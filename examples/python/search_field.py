class SearchField:
    def __init__(self):
        self.query = ""

    def on_input(self, text):
        self.query = text

    def render(self):
        return '''<div class="toolbar">
<input type="text" id="searchInput" placeholder="Search..." onkeydown="if(event.key==='Enter')doSearch()">
<button class="btn-search" onclick="doSearch()">Search</button>
<button class="btn-reset" onclick="location.href='/'">Reset</button>
</div>'''


class ButtonSearch:
    def __init__(self, table, field):
        self.table = table
        self.field = field
        self.results = []

    def on_click(self):
        self.results = self.table.search(self.field.query)

    def get_results(self):
        return list(self.results)
