"""Menu list layout helpers."""


def compute_window_start(selected_index, item_count, visible_rows, preview_rows=1):
    """Compute list window start for a selected row.

    `preview_rows` reserves rows below the selected item so the user can see what is next.
    """
    if visible_rows <= 0 or item_count <= visible_rows:
        return 0

    anchor_row = visible_rows - 1 - preview_rows
    if anchor_row < 0:
        anchor_row = 0

    start = selected_index - anchor_row
    if start < 0:
        start = 0

    max_start = item_count - visible_rows
    if start > max_start:
        start = max_start

    return start
