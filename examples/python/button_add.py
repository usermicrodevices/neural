import html as _html


class Form:
    def __init__(self):
        self.fields = {}

    def clear(self):
        self.fields.clear()

    def get_field(self, name):
        return self.fields.get(name, "")

    def set_field(self, name, value):
        self.fields[name] = value


class ButtonAdd:
    def __init__(self, table, form):
        self.table = table
        self.form = form

    def on_click(self):
        if not self.form.get_field("name"):
            return
        self.table.add_record(self.form.fields)
        self.form.clear()

    def render(self, columns):
        fields_html = "".join(
            f'<label>{_html.escape(c)}<input type="text" id="f_{_html.escape(c)}" required></label>'
            for c in columns
        )
        return f'''<button class="btn-add" onclick="showAdd()">+ Add Record</button>
<div class="modal-bg" id="addModal" onclick="if(event.target===this)this.style.display='none'">
<div class="modal">
<h3>Add Record</h3>
<form onsubmit="return saveAdd(event)">
{fields_html}
<div class="modal-btns">
<button type="button" class="cancel" onclick="document.getElementById('addModal').style.display='none'">Cancel</button>
<button type="submit" class="save">Save</button>
</div></form></div></div>'''
