/*
 * Settings in the radio's flash (board::storage_*). Every write holds the
 * channel output for its duration.
 */

#pragma once

namespace storage {

/* Take each setting from the storage, or its default if it is not stored or out of range. */
void load_settings();

/* Store all settings; only values that differ from their default take up space. */
bool save_settings();

/* Store only the trim (trim.*), leaving other unsaved changes unsaved. */
bool save_trim();

/* Store one setting, leaving other unsaved changes unsaved. */
bool save_setting(int index);

} // namespace storage
