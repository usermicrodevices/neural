class ButtonDelete:
    def __init__(self, table):
        self.table = table
        self.selected_row_id = -1

    def on_click(self, row_id):
        self.table.remove_record(row_id)
        if self.selected_row_id == row_id:
            self.selected_row_id = -1

    def confirm(self):
        if self.selected_row_id >= 0:
            self.on_click(self.selected_row_id)

    def render(self):
        return ''
