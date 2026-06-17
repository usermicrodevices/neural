import html as _html


class TableRowTitle:
    def __init__(self, text="", row_index=0):
        self.text = text
        self.alignment = "left"
        self.row_index = row_index
        self.is_header = False

    def set_text(self, t):
        self.text = t

    def set_alignment(self, a):
        self.alignment = a

    def render(self):
        tag = "th" if self.is_header else "td"
        return f"<{tag} style=\"text-align:{self.alignment}\">{_html.escape(self.text)}</{tag}>"

    def mark_as_header(self):
        self.is_header = True
