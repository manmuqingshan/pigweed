# Copyright 2021 The Pigweed Authors
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
"""Table view renderer for LogLines."""

import collections
import copy
import logging
from typing import Iterable

from prompt_toolkit.formatted_text import (
    ANSI,
    OneStyleAndTextTuple,
    StyleAndTextTuples,
)

from pw_console.console_prefs import ConsolePrefs
from pw_console.log_line import LogLine
from pw_console.text_formatting import strip_ansi

_LOG = logging.getLogger(__package__)


class TableView:
    """Store column information and render logs into formatted tables."""

    # TODO(tonymd): Add a method to provide column formatters externally.
    # Should allow for string format, column color, and column ordering.
    FLOAT_FORMAT = '%.3f'
    INT_FORMAT = '%s'
    LAST_TABLE_COLUMN_NAMES = ['msg', 'message']

    def __init__(self, prefs: ConsolePrefs):
        self.column_widths: collections.OrderedDict = collections.OrderedDict()
        self._header_fragment_cache: list[StyleAndTextTuples] = []

        # Assume common defaults here before recalculating in set_formatting().
        self._default_time_width: int = 17
        self.column_widths['time'] = self._default_time_width
        self.column_widths['level'] = 3
        self._year_month_day_width: int = 9

        self.column_names: list[str] = []
        self.hidden_columns: dict[str, bool] = {}
        self.apply_max_column_width: bool = True

        # Set prefs last to override defaults.
        self.set_prefs(prefs)

    def set_prefs(self, prefs: ConsolePrefs) -> None:
        self.prefs = prefs
        self.column_padding = ' ' * self.prefs.spaces_between_columns

        # Set columns hidden based on legacy hide column prefs.
        if not self.prefs.show_python_file:
            self.hidden_columns['py_file'] = True
        if not self.prefs.show_python_logger:
            self.hidden_columns['py_logger'] = True
        if not self.prefs.show_source_file:
            self.hidden_columns['file'] = True

        # Apply default visibility for any settings in the prefs.
        for column_name, is_visible in self.prefs.column_visibility.items():
            is_hidden = not is_visible
            self.hidden_columns[column_name] = is_hidden

    def is_column_hidden(self, name: str) -> bool:
        return self.hidden_columns.get(name, False)

    def set_column_hidden(self, name: str, hidden: bool = True) -> None:
        self.hidden_columns[name] = hidden
        self._update_table_header()

    def all_column_names(self) -> Iterable[str]:
        yield from self.column_names

    def _ordered_column_widths(self) -> dict[str, int]:
        """Return each column and width in the preferred order."""
        if self.prefs.column_order:
            # Get ordered_columns
            columns = copy.copy(self.column_widths)
            ordered_columns = {}

            for column_name in self.prefs.column_order:
                # If valid column name
                if column_name in columns:
                    ordered_columns[column_name] = columns.pop(column_name)

            # Add remaining columns unless user preference to hide them.
            if not self.prefs.omit_unspecified_columns:
                for column_name in columns:
                    ordered_columns[column_name] = columns[column_name]
        else:
            ordered_columns = copy.copy(self.column_widths)

        for column_name, is_hidden in self.hidden_columns.items():
            if is_hidden and column_name in ordered_columns:
                del ordered_columns[column_name]

        return ordered_columns

    def update_column_widths(
        self, new_column_widths: collections.OrderedDict
    ) -> None:
        """Calculate the max widths for each metadata field."""
        self.column_widths.update(new_column_widths)

        # If max column width is enabled.
        if self.apply_max_column_width:
            for name, width in self.prefs.column_width.items():
                # If column is present, set the width from preferences
                if name in self.column_widths:
                    self.column_widths[name] = width

        self._update_table_header()

    def _update_table_header(self) -> None:
        self.column_names = [
            name for name, _width in self.column_widths.items() if name != 'msg'
        ] + ['message']

        default_style = 'bold'
        fragments: collections.deque = collections.deque()

        # Update time column width to current prefs setting
        self.column_widths['time'] = self._default_time_width
        if self.prefs.hide_date_from_log_time:
            self.column_widths['time'] = (
                self._default_time_width - self._year_month_day_width
            )

        for name, width in self._ordered_column_widths().items():
            # These fields will be shown at the end
            if name in TableView.LAST_TABLE_COLUMN_NAMES:
                continue

            fragments.append((default_style, name.title()[:width].ljust(width)))
            fragments.append(('', self.column_padding))

        fragments.append((default_style, 'Message'))

        self._header_fragment_cache = list(fragments)

    def formatted_header(self) -> list[StyleAndTextTuples]:
        """Get pre-formatted table header."""
        return self._header_fragment_cache

    def formatted_row(self, log: LogLine) -> StyleAndTextTuples:
        """Render a single table row."""
        # pylint: disable=too-many-locals
        padding_formatted_text = ('', self.column_padding)
        # Don't apply any background styling that would override the parent
        # window or selected-log-line style.
        default_style = ''

        table_fragments: StyleAndTextTuples = []

        # NOTE: To preseve ANSI formatting on log level use:
        # table_fragments.extend(
        #     ANSI(log.record.levelname.ljust(
        #         self.column_widths['level'])).__pt_formatted_text__())

        # Collect remaining columns to display after host time and level.
        columns: dict[str, str | tuple[str, str]] = {}
        for name, width in self._ordered_column_widths().items():
            # Skip these modifying these fields
            if name in TableView.LAST_TABLE_COLUMN_NAMES:
                continue

            # hasattr checks are performed here since a log record may not have
            # asctime or levelname if they are not included in the formatter
            # fmt string.
            if name == 'time' and hasattr(log.record, 'asctime'):
                time_text = log.record.asctime
                if self.prefs.hide_date_from_log_time:
                    time_text = time_text[self._year_month_day_width :]
                time_style = self.prefs.column_style(
                    'time', time_text, default='class:log-time'
                )
                columns['time'] = (
                    time_style,
                    time_text.ljust(self.column_widths['time']),
                )
                continue

            if name == 'level' and hasattr(log.record, 'levelname'):
                # Remove any existing ANSI formatting and apply our colors.
                level_text = strip_ansi(log.record.levelname)
                level_style = self.prefs.column_style(
                    'level',
                    level_text,
                    default='class:log-level-{}'.format(log.record.levelno),
                )
                columns['level'] = (
                    level_style,
                    level_text.ljust(self.column_widths['level']),
                )
                continue

            value = ' '
            # If fields are populated, grab the metadata column.
            if hasattr(log.metadata, 'fields'):
                value = log.metadata.fields.get(name, ' ')
            if value is None:
                value = ' '
            left_justify = True

            # Right justify and format numbers
            if isinstance(value, float):
                value = TableView.FLOAT_FORMAT % value
                left_justify = False
            elif isinstance(value, int):
                value = TableView.INT_FORMAT % value
                left_justify = False

            if left_justify:
                columns[name] = value.ljust(width)
            else:
                columns[name] = value.rjust(width)

        # Grab the message to appear after the justified columns.
        # Default to the Python log message
        message_text = log.record.message.rstrip()

        # If fields are populated, grab the msg field.
        if hasattr(log.metadata, 'fields'):
            message_text = log.metadata.fields.get('msg', message_text)
        ansi_stripped_message_text = strip_ansi(message_text)

        # Add to columns for width calculations with ansi sequences removed.
        columns['message'] = ansi_stripped_message_text

        index_modifier = 0
        # Go through columns and convert to FormattedText where needed.
        for i, column in enumerate(columns.items()):
            column_name, column_value = column

            # If max column width is enabled.
            if self.apply_max_column_width:
                # Truncate the column width if set in prefs.
                if column_name in self.prefs.column_width:
                    max_width = self.prefs.column_width[column_name]
                    column_value = column_value[:max_width]  # type: ignore

            # Skip the message column in this loop.
            if column_name == 'message':
                continue

            if i in [0, 1] and column_name in ['time', 'level']:
                index_modifier -= 1
            # For raw strings that don't have their own ANSI colors, apply the
            # theme color style for this column.
            if isinstance(column_value, str):
                fallback_style = (
                    'class:log-table-column-{}'.format(i + index_modifier)
                    if 0 <= i <= 7
                    else default_style
                )

                style = self.prefs.column_style(
                    column_name, column_value.rstrip(), default=fallback_style
                )

                table_fragments.append((style, column_value))
                table_fragments.append(padding_formatted_text)
            # Add this tuple to table_fragments.
            elif isinstance(column, tuple):
                table_fragments.append(column_value)
                # Add padding if not the last column.
                if i < len(columns) - 1:
                    table_fragments.append(padding_formatted_text)

        # Handle message column.
        if self.prefs.recolor_log_lines_to_match_level:
            message_style = default_style
            if log.record.levelno >= 30:  # Warning, Error and Critical
                # Style the whole message to match the level
                message_style = 'class:log-level-{}'.format(log.record.levelno)

            message: OneStyleAndTextTuple = (
                message_style,
                ansi_stripped_message_text,
            )
            table_fragments.append(message)
        else:
            # Format the message preserving any ANSI color sequences.
            message_fragments = ANSI(message_text).__pt_formatted_text__()
            table_fragments.extend(message_fragments)

        # Add the final new line for this row.
        table_fragments.append(('', '\n'))
        return table_fragments
