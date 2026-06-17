import html as _html


class TableColumn:
    def __init__(self, index=0, title=""):
        self.index = index
        self.title = title
        self.data_type = "text"
        self.width = 100
        self.sortable = False
        self.visible = True

    def set_title(self, t):
        self.title = t

    def set_width(self, w):
        self.width = w

    def render_header(self):
        if not self.visible:
            return ""
        style = f' style="width:{self.width}px"' if self.width else ""
        return f"<th{style}>{_html.escape(self.title)}</th>"

    def render_cell(self, value):
        if not self.visible:
            return ""
        return f"<td>{_html.escape(str(value))}</td>"

    def sort(self, rows):
        return sorted(rows, key=lambda r: r.get_cell(self.index).value)

    def hide(self):
        self.visible = False

    def show(self):
        self.visible = True
