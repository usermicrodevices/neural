import html as _html


class Cell:
    def __init__(self, value="", data_type="text"):
        self.value = value
        self.data_type = data_type

    def get_display(self):
        return self.value

    def set_value(self, v):
        self.value = v

    def render(self):
        return f"<td>{_html.escape(str(self.value))}</td>"


class TableRow:
    def __init__(self, index=0):
        self.index = index
        self.cells = []
        self.selected = False

    def set_index(self, i):
        self.index = i

    def get_cell(self, col):
        if 0 <= col < len(self.cells):
            return self.cells[col]
        return Cell()

    def add_cell(self, cell):
        self.cells.append(cell)

    def remove_cell(self, col):
        if 0 <= col < len(self.cells):
            self.cells.pop(col)

    def select(self):
        self.selected = True

    def deselect(self):
        self.selected = False

    def render(self, record, columns, actions_html=""):
        cls = ' class="selected"' if self.selected else ""
        cells = "".join(
            f"<td>{_html.escape(str(record.get(c, '')))}</td>" for c in columns
        )
        return f"<tr{cls}><td class=\"id-col\">{record.get('id','')}</td>{cells}<td class=\"actions\">{actions_html}</td></tr>"
