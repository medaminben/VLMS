#pragma once

/// Which rows a list query sees. Live is every page's default; Archived is the
/// Archive page; Any is for history views, which must not lose archived rows.
enum class ArchiveScope {
    Live,
    Archived,
    Any,
};
