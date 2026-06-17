import html as _html


class DataTableColumnTitle:
    def __init__(self, text="", column_index=0):
        self.text = text
        self.alignment = "left"
        self.column_index = column_index
        self.sortable = False
        self.is_filterable = False

    def set_text(self, t):
        self.text = t

    def set_alignment(self, a):
        self.alignment = a

    def render(self):
        cls = ' class="sortable"' if self.sortable else ""
        return f"<th{cls}>{_html.escape(self.text)}</th>"

    def enable_sort(self):
        self.sortable = True

    def enable_filter(self):
        self.is_filterable = True
