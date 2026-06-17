#!/usr/bin/env python3
"""
Abstract DataTable Web Application
Composed from UML component sources with SQLite backend.
Schema is configured by the client via /api/configure.

Usage:
    python3 server.py [--port 8888] [--db data.db]
"""

import argparse
import json
import os
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from data_table import DataTable
from button_add import ButtonAdd, Form
from button_edit import ButtonEdit
from button_delete import ButtonDelete
from search_field import SearchField, ButtonSearch
from table_row import TableRow
from table_column import TableColumn
from table_column_title import DataTableColumnTitle as TableColumnTitle
from table_row_title import TableRowTitle
from context_menu import ContextMenu, MenuItem


class PageAssembler:
    """Assembles HTML page from UML components."""

    def __init__(self, table, title="DataTable"):
        self.table = table
        self.title = title
        self._build_components()

    def _build_components(self):
        self.columns = self.table.columns
        self.form = Form()
        self.btn_add = ButtonAdd(self.table, self.form)
        self.btn_edit = ButtonEdit(self.table, self.form)
        self.btn_delete = ButtonDelete(self.table)
        self.search = SearchField()
        self.btn_search = ButtonSearch(self.table, self.search)
        self.ctx = ContextMenu()
        self.col_titles = [
            TableColumnTitle(c, i) for i, c in enumerate(self.columns)
        ]
        self.table_row = TableRow()

    def render_css(self):
        return """<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#f0f2f5;padding:20px}
h1{color:#333;margin-bottom:16px;font-size:20px}
.toolbar{display:flex;gap:8px;margin-bottom:16px;align-items:center;flex-wrap:wrap}
.toolbar input[type=text]{padding:6px 10px;border:1px solid #ccc;border-radius:4px;font-size:13px;width:200px}
.toolbar button{padding:6px 14px;border:none;border-radius:4px;cursor:pointer;font-size:13px}
.btn-add{background:#4CAF50;color:#fff}.btn-add:hover{background:#43a047}
.btn-search{background:#2196F3;color:#fff}.btn-search:hover{background:#1e88e5}
.btn-reset{background:#9e9e9e;color:#fff}.btn-reset:hover{background:#757575}
table{width:100%;border-collapse:collapse;background:#fff;border-radius:6px;overflow:hidden;box-shadow:0 1px 3px rgba(0,0,0,.1)}
th{background:#f5f5f5;padding:10px 12px;text-align:left;font-size:12px;color:#666;border-bottom:2px solid #e0e0e0}
td{padding:8px 12px;border-bottom:1px solid #eee;font-size:13px}
tr:hover{background:#f9f9f9}.id-col{width:50px;color:#999}.actions{width:100px}
.actions button{padding:3px 8px;font-size:11px;border:none;border-radius:3px;cursor:pointer;margin-right:4px}
.btn-edit{background:#FF9800;color:#fff}.btn-del{background:#f44336;color:#fff}
.msg{padding:8px 12px;margin-bottom:12px;background:#e8f5e9;border-radius:4px;color:#2e7d32;font-size:13px}
.modal-bg{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.4);z-index:100}
.modal{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);background:#fff;padding:20px;border-radius:8px;min-width:320px;box-shadow:0 4px 20px rgba(0,0,0,.2)}
.modal h3{margin-bottom:12px;font-size:16px}
.modal label{display:block;margin-bottom:8px;font-size:13px;color:#555}
.modal input[type=text]{width:100%;padding:6px 10px;border:1px solid #ccc;border-radius:4px;font-size:13px;margin-bottom:4px}
.modal-btns{display:flex;gap:8px;margin-top:12px;justify-content:flex-end}
.modal-btns button{padding:6px 16px;border:none;border-radius:4px;cursor:pointer;font-size:13px}
.modal-btns .save{background:#4CAF50;color:#fff}
.modal-btns .cancel{background:#9e9e9e;color:#fff}
.status{margin-top:12px;font-size:12px;color:#999}
.ctx-menu{position:fixed;background:#fff;border:1px solid #ccc;border-radius:4px;box-shadow:0 2px 8px rgba(0,0,0,.15);z-index:200;display:none;min-width:140px}
.ctx-item{padding:6px 14px;cursor:pointer;font-size:12px}
.ctx-item:hover{background:#e3f2fd}
.ctx-sep{height:1px;background:#eee;margin:2px 0}
</style>"""

    def render_table_headers(self):
        ths = '<th class="id-col">ID</th>'
        for ct in self.col_titles:
            ths += ct.render()
        ths += '<th class="actions">Actions</th>'
        return f"<thead><tr>{ths}</tr></thead>"

    def render_table_rows_js(self):
        return """function renderRows(data){
var t=document.getElementById("tbody");
t.innerHTML=data.map(function(r){
var cells=COLUMNS.map(function(c){return "<td>"+esc(r[c]||"")+"</td>}).join("");
return "<tr><td class=\\"id-col\\">"+r.id+"</td>"+cells+
"<td class=\\"actions\\"><button class=\\"btn-edit\\" onclick=\\"showEdit("+r.id+")\\">Edit</button>"+
"<button class=\\"btn-del\\" onclick=\\"delRecord("+r.id+")\\">Del</button></td></tr>";
}).join("");
document.getElementById("status").textContent=data.length+" record(s)";
}
function esc(s){var d=document.createElement("div");d.textContent=s;return d.innerHTML}
function doSearch(){var q=document.getElementById("searchInput").value.trim().toLowerCase();
if(!q){renderRows(ROWS);return}
renderRows(ROWS.filter(function(r){return COLUMNS.some(function(c){return(r[c]||"").toLowerCase().indexOf(q)>=0})}));
}
function showAdd(){document.getElementById("addModal").style.display="block"}
function showEdit(id){var r=ROWS.find(function(x){return x.id===id});if(!r)return;
document.getElementById("editId").value=id;
COLUMNS.forEach(function(c){var el=document.getElementById("ef_"+c);if(el)el.value=r[c]||""});
document.getElementById("editModal").style.display="block"}
function delRecord(id){if(!confirm("Delete #"+id+"?"))return;
fetch("/api/delete/"+id,{method:"POST"}).then(function(){location.reload()})}
function saveAdd(e){e.preventDefault();var f={};
COLUMNS.forEach(function(c){f[c]=document.getElementById("f_"+c).value});
fetch("/api/add",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(f)})
.then(function(){location.reload()});return false}
function saveEdit(e){e.preventDefault();var id=document.getElementById("editId").value;var f={};
COLUMNS.forEach(function(c){var el=document.getElementById("ef_"+c);if(el)f[c]=el.value});
fetch("/api/update/"+id,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(f)})
.then(function(){location.reload()});return false}"""

    def render(self, rows, message=None):
        cols_json = json.dumps(self.columns)
        rows_json = json.dumps(rows)
        msg_html = f'<div class="msg">{message}</div>' if message else ""

        return f"""<!DOCTYPE html>
<html><head><meta charset="UTF-8"><title>{self.title}</title>
{self.render_css()}
</head><body>
<h1>{self.title}</h1>
{msg_html}
{self.search.render()}
{self.btn_add.render(self.columns)}
{self.btn_edit.render(self.columns)}
<table>{self.render_table_headers()}<tbody id="tbody"></tbody></table>
<div class="status" id="status"></div>
<script>
var COLUMNS={cols_json};
var ROWS={rows_json};
{self.render_table_rows_js()}
renderRows(ROWS);
</script>
</body></html>"""


class DataTableHandler(BaseHTTPRequestHandler):
    assembler: PageAssembler = None

    def log_message(self, fmt, *args):
        pass

    def _send(self, code, content_type, body):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self):
        length = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(length)

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/" or path == "":
            rows = self.assembler.table.find_all()
            page = self.assembler.render(rows)
            self._send(200, "text/html", page)
            return

        if path == "/api/list":
            rows = self.assembler.table.find_all()
            self._send(200, "application/json", json.dumps(rows))
            return

        if path == "/api/schema":
            self._send(200, "application/json", json.dumps({
                "table_name": self.assembler.table.table_name,
                "columns": self.assembler.table.columns
            }))
            return

        if path.startswith("/api/search"):
            qs = parse_qs(parsed.query)
            q = qs.get("q", [""])[0]
            rows = self.assembler.table.search(q) if q else self.assembler.table.find_all()
            self._send(200, "application/json", json.dumps(rows))
            return

        self._send(404, "text/plain", "Not found")

    def do_POST(self):
        path = urlparse(self.path).path

        if path == "/api/configure":
            try:
                data = json.loads(self._read_body())
            except Exception:
                self._send(400, "application/json", '{"error":"invalid json"}')
                return
            table_name = data.get("table_name", "records")
            columns = data.get("columns", [])
            if not columns:
                self._send(400, "application/json", '{"error":"columns required"}')
                return
            old_table = self.assembler.table
            old_table.close()
            new_table = DataTable(
                db_path=old_table.db_path,
                table_name=table_name,
                columns=columns
            )
            self.assembler.table = new_table
            self.assembler = PageAssembler(new_table, title=self.assembler.title)
            self._send(200, "application/json", '{"status":"ok"}')
            return

        if path == "/api/add":
            try:
                data = json.loads(self._read_body())
            except Exception:
                self._send(400, "application/json", '{"error":"invalid json"}')
                return
            rid = self.assembler.table.add_record(data)
            self._send(200, "application/json", json.dumps({"id": rid}))
            return

        if path.startswith("/api/update/"):
            try:
                rid = int(path.split("/")[-1])
            except ValueError:
                self._send(400, "application/json", '{"error":"invalid id"}')
                return
            try:
                data = json.loads(self._read_body())
            except Exception:
                self._send(400, "application/json", '{"error":"invalid json"}')
                return
            self.assembler.table.update_record(rid, data)
            self._send(200, "application/json", '{"status":"ok"}')
            return

        if path.startswith("/api/delete/"):
            try:
                rid = int(path.split("/")[-1])
            except ValueError:
                self._send(400, "application/json", '{"error":"invalid id"}')
                return
            self.assembler.table.remove_record(rid)
            self._send(200, "application/json", '{"status":"ok"}')
            return

        self._send(404, "text/plain", "Not found")


def main():
    parser = argparse.ArgumentParser(description="Abstract DataTable Web App")
    parser.add_argument("--port", type=int, default=8888)
    parser.add_argument("--db", default="data.db")
    args = parser.parse_args()

    table = DataTable(db_path=args.db, table_name="records", columns=["name"])
    assembler = PageAssembler(table, title="DataTable")

    DataTableHandler.assembler = assembler

    server = HTTPServer(("0.0.0.0", args.port), DataTableHandler)
    print(f"Server: http://0.0.0.0:{args.port}")
    print(f"DB: {args.db}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        table.close()
        server.server_close()


if __name__ == "__main__":
    main()
