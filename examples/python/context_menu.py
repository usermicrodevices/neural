import html as _html


class MenuItem:
    def __init__(self, label="", icon=""):
        self.label = label
        self.icon = icon
        self.enabled = True
        self.separator = False
        self.on_click = None

    def set_enabled(self, e):
        self.enabled = e

    def render(self):
        prefix = (self.icon + " ") if self.icon else ""
        return prefix + self.label


class ContextMenu:
    def __init__(self):
        self.x = 0
        self.y = 0
        self.visible = False
        self.items = []

    def show(self, px, py):
        self.x = px
        self.y = py
        self.visible = True

    def hide(self):
        self.visible = False

    def add_item(self, item):
        self.items.append(item)

    def remove_item(self, label):
        self.items = [i for i in self.items if i.label != label]

    def hit_test(self, mx, my):
        if not self.visible:
            return None
        cy = self.y
        for item in self.items:
            if item.separator:
                cy += 4
                continue
            if self.x <= mx <= self.x + 160 and cy <= my <= cy + 24:
                return item if item.enabled else None
            cy += 24
        return None

    def render(self):
        if not self.items:
            return ""
        items_html = "".join(
            '<div class="ctx-sep"></div>' if it.separator
            else f'<div class="ctx-item" onclick="{it.on_click or ""}">{_html.escape(it.render())}</div>'
            for it in self.items
        )
        return f'''<div id="ctxMenu" class="ctx-menu">{items_html}</div>'''
