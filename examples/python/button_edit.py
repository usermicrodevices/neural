import html as _html


class ButtonEdit:
    def __init__(self, table, form):
        self.table = table
        self.form = form

    def on_click(self, row_id):
        r = self.table.find_by_id(row_id)
        if r:
            self.form.fields = {k: v for k, v in r.items() if k != "id"}

    def render(self, columns):
        fields_html = "".join(
            f'<label>{_html.escape(c)}<input type="text" id="ef_{_html.escape(c)}" required></label>'
            for c in columns
        )
        return f'''<div class="modal-bg" id="editModal" onclick="if(event.target===this)this.style.display='none'">
<div class="modal">
<h3>Edit Record</h3>
<form onsubmit="return saveEdit(event)">
<input type="hidden" id="editId">
{fields_html}
<div class="modal-btns">
<button type="button" class="cancel" onclick="document.getElementById('editModal').style.display='none'">Cancel</button>
<button type="submit" class="save">Update</button>
</div></form></div></div>'''
