/*
 * Copyright 2025 The Pigweed Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

/* This relatively simplified linker script will work with many RISC-V cores
 * that have on-board memory-mapped RAM and FLASH. For more
 * complex projects and devices, it's possible this linker script will not be
 * sufficient as-is.
 *
 * This linker script is likely not suitable for a project with a bootloader.
 */

/* Note: This technically doesn't set the firmware's entry point. Setting the
 *       firmware entry point is done by setting vector_table[1]
 *       (Reset_Handler). However, this DOES tell the compiler how to optimize
 *       when --gc-sections is enabled.
 */
/* ENTRY(Reset) */ /*TODO: is this needed???*/

MEMORY
{
  /* TODO: b/234892223 - Make it possible for projects to freely customize
   * memory regions.
   */

  /* Internal Flash */
  FLASH(rx) : ORIGIN = 0x80000000, LENGTH = 255K
  /* Internal SRAM */
  RAM(rwx) : ORIGIN = 0x81000000, LENGTH = 64K

  /* Each memory region above has an associated .*.unused_space section that
   * overlays the unused space at the end of the memory segment. These segments
   * are used by pw_bloat.bloaty_config to create the utilization data source
   * for bloaty size reports.
   *
   * These sections MUST be located immediately after the last section that is
   * placed in the respective memory region or lld will issue a warning like:
   *
   *   warning: ignoring memory region assignment for non-allocatable section
   *      '.FLASH.unused_space'
   *
   * If this warning occurs, it's also likely that LLD will have created quite
   * large padded regions in the ELF file due to bad cursor operations. This
   * can cause ELF files to balloon from hundreds of kilobytes to hundreds of
   * megabytes.
   *
   * Attempting to add sections to the memory region AFTER the unused_space
   * section will cause the region to overflow.
   */
}

SECTIONS
{
  /* Main executable code. */
  .code : ALIGN(4)
  {
    /* Put reset handler first in .text section so it ends up as the entry */
    /* point of the program. */
    KEEP(*(.init));
    KEEP(*(.init.rust));
    . = ALIGN(4);
    /* Application code. */
    *(.text)
    *(.text*)
    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    /* Constants.*/
    *(.rodata)
    *(.rodata*)

    /* .preinit_array, .init_array, .fini_array are used by libc.
     * Each section is a list of function pointers that are called pre-main and
     * post-exit for object initialization and tear-down.
     * Since the region isn't explicitly referenced, specify KEEP to prevent
     * link-time garbage collection. SORT is used for sections that have strict
     * init/de-init ordering requirements. */
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);

    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);

    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* Explicitly initialized global and static data. (.data)*/
  .static_init_ram : ALIGN(4)
  {
    *(.data)
    *(.data*)
    . = ALIGN(4);
  } >RAM AT> FLASH

  /* Represents unused space in the FLASH segment. This MUST be the last section
   * assigned to the FLASH region.
   */
  .FLASH.unused_space (NOLOAD) : ALIGN(4)
  {
    . = ABSOLUTE(ORIGIN(FLASH) + LENGTH(FLASH));
  } >FLASH

  /* The .zero_init_ram, .heap, and .stack sections below require (NOLOAD)
   * annotations for LLVM lld, but not GNU ld, because LLVM's lld intentionally
   * interprets the linker file differently from ld:
   *
   * https://discourse.llvm.org/t/lld-vs-ld-section-type-progbits-vs-nobits/5999/3
   *
   * Zero initialized global/static data (.bss) is initialized in
   * pw_boot_Entry() via memset(), so the section doesn't need to be loaded from
   * flash. The .heap and .stack sections don't require any initialization,
   * as they only represent allocated memory regions, so they also do not need
   * to be loaded.
   */
  .zero_init_ram (NOLOAD) : ALIGN(4)
  {
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
  } >RAM

  .heap (NOLOAD) : ALIGN(4)
  {
    pw_boot_heap_low_addr = .;
    . = . + 0;
    . = ALIGN(4);
    pw_boot_heap_high_addr = .;
  } >RAM

  /* Link-time check for stack overlaps.
   *
   */
  .stack (NOLOAD) :
  {
    /* Set the address that the main stack pointer should be initialized to. */
    pw_boot_stack_low_addr = .;
    HIDDEN(_stack_size = ORIGIN(RAM) + LENGTH(RAM) - .);
    /* Align the stack to a lower address to ensure it isn't out of range. */
    HIDDEN(_stack_high = (. + _stack_size) & ~0x7);
    ASSERT(_stack_high - . >= 1K,
           "Error: Not enough RAM for desired minimum stack size.");
    . = _stack_high;
    pw_boot_stack_high_addr = .;
  } >RAM

  /* Represents unused space in the RAM segment. This MUST be the last section
   * assigned to the RAM region.
   */
  .RAM.unused_space (NOLOAD) : ALIGN(4)
  {
    . = ABSOLUTE(ORIGIN(RAM) + LENGTH(RAM));
  } >RAM

  /* Strip unnecessary stuff */
  /DISCARD/ : { *(.comment .note .eh_frame .eh_frame_hdr) }
}

/* Symbols used by core_init.c: */
/* Start of .static_init_ram in FLASH. */
_pw_static_init_flash_start = LOADADDR(.static_init_ram);

/* Region of .static_init_ram in RAM. */
_pw_static_init_ram_start = ADDR(.static_init_ram);
_pw_static_init_ram_end = _pw_static_init_ram_start + SIZEOF(.static_init_ram);

/* Region of .zero_init_ram. */
_pw_zero_init_ram_start = ADDR(.zero_init_ram);
_pw_zero_init_ram_end = _pw_zero_init_ram_start + SIZEOF(.zero_init_ram);

/* Symbols needed for the Rust riscv-rt crate. */
_sbss = _pw_zero_init_ram_start;
_ebss = _pw_zero_init_ram_end;
_sdata = _pw_static_init_ram_start;
_edata = _pw_static_init_ram_end;
_sidata = LOADADDR(.static_init_ram);
REGION_ALIAS("REGION_TEXT", FLASH);
REGION_ALIAS("REGION_RODATA", FLASH);
REGION_ALIAS("REGION_DATA", RAM);
REGION_ALIAS("REGION_BSS", RAM);
REGION_ALIAS("REGION_HEAP", RAM);
REGION_ALIAS("REGION_STACK", RAM);

_stext = ORIGIN(REGION_TEXT);
_heap_size = 1K; /* Set heap size to 1KB */
_max_hart_id = 1; /* Two harts present */
_hart_stack_size = 1K; /* Set stack size per hart to 1KB */
_stack_start = _stack_high;

PROVIDE(InstructionMisaligned = ExceptionHandler);
PROVIDE(InstructionFault = ExceptionHandler);
PROVIDE(IllegalInstruction = ExceptionHandler);
PROVIDE(Breakpoint = ExceptionHandler);
PROVIDE(LoadMisaligned = ExceptionHandler);
PROVIDE(LoadFault = ExceptionHandler);
PROVIDE(StoreMisaligned = ExceptionHandler);
PROVIDE(StoreFault = ExceptionHandler);;
PROVIDE(UserEnvCall = ExceptionHandler);
PROVIDE(SupervisorEnvCall = ExceptionHandler);
PROVIDE(MachineEnvCall = ExceptionHandler);
PROVIDE(InstructionPageFault = ExceptionHandler);
PROVIDE(LoadPageFault = ExceptionHandler);
PROVIDE(StorePageFault = ExceptionHandler);

PROVIDE(SupervisorSoft = DefaultHandler);
PROVIDE(MachineSoft = DefaultHandler);
PROVIDE(SupervisorTimer = DefaultHandler);
PROVIDE(MachineTimer = DefaultHandler);
PROVIDE(SupervisorExternal = DefaultHandler);
PROVIDE(MachineExternal = DefaultHandler);

PROVIDE(DefaultHandler = DefaultInterruptHandler);
PROVIDE(ExceptionHandler = DefaultExceptionHandler);

PROVIDE(__pre_init = default_pre_init);
PROVIDE(_setup_interrupts = default_setup_interrupts);
PROVIDE(_mp_hook = default_mp_hook);
PROVIDE(_start_trap = default_start_trap);

/* These symbols are used by pw_bloat.bloaty_config to create the memoryregions
 * data source for bloaty in this format (where the optional _N defaults to 0):
 * pw_bloat_config_memory_region_NAME_{start,end}{_N,} */
pw_bloat_config_memory_region_FLASH_start = ORIGIN(FLASH);
pw_bloat_config_memory_region_FLASH_end = ORIGIN(FLASH) + LENGTH(FLASH);
pw_bloat_config_memory_region_RAM_start = ORIGIN(RAM);
pw_bloat_config_memory_region_RAM_end = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  /*
   * This section stores metadata that may be used during tokenized string
   * decoding. This metadata describes properties that may affect how the
   * tokenized string is encoded or decoded -- the maximum length of the hash
   * function and the sizes of certain integer types.
   *
   * Metadata is declared as key-value pairs. See the metadata variable in
   * tokenize.cc for further details.
   */
  .pw_tokenizer.info 0x0 (INFO) :
  {
    KEEP(*(.pw_tokenizer.info))
  }

  /*
   * Tokenized string entries are stored in this section. Each entry contains
   * the original string literal and the calculated token that represents it. In
   * the compiled code, the token and a compact argument list encoded in a
   * uint32_t are used in place of the format string. The compiled code
   * contains no references to the tokenized string entries in this section.
   *
   * The tokenized string entry format is specified by the
   * pw::tokenizer::internal::Entry class in
   * pw_tokenizer/public/pw_tokenizer/internal/tokenize_string.h.
   *
   * The section contents are declared with KEEP so that they are not removed
   * from the ELF. These are never emitted in the final binary or loaded into
   * memory.
   */
  .pw_tokenizer.entries 0x0 (INFO) :
  {
    KEEP(*(.pw_tokenizer.entries.*))
  }
}
