import sqlite3
import json
import os


class DataTable:

    def __init__(self, db_path="data.db", table_name="records", columns=None):
        self.db_path = db_path
        self.table_name = table_name
        self.columns = columns or ["name", "value"]
        self.conn = sqlite3.connect(db_path, check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA journal_mode=WAL")
        self._create_table()

    def _create_table(self):
        col_defs = ", ".join(f"{c} TEXT" for c in self.columns)
        self.conn.execute(
            f"CREATE TABLE IF NOT EXISTS {self.table_name} "
            f"(id INTEGER PRIMARY KEY AUTOINCREMENT, {col_defs})"
        )
        self.conn.commit()

    def add_record(self, fields):
        cols = [c for c in self.columns if c in fields]
        if not cols:
            return None
        placeholders = ", ".join("?" * len(cols))
        col_names = ", ".join(cols)
        vals = [fields[c] for c in cols]
        cur = self.conn.execute(
            f"INSERT INTO {self.table_name} ({col_names}) VALUES ({placeholders})", vals
        )
        self.conn.commit()
        return cur.lastrowid

    def remove_record(self, record_id):
        self.conn.execute(f"DELETE FROM {self.table_name} WHERE id=?", (record_id,))
        self.conn.commit()

    def update_record(self, record_id, fields):
        sets = []
        vals = []
        for c in self.columns:
            if c in fields:
                sets.append(f"{c}=?")
                vals.append(fields[c])
        if not sets:
            return
        vals.append(record_id)
        self.conn.execute(
            f"UPDATE {self.table_name} SET {', '.join(sets)} WHERE id=?", vals
        )
        self.conn.commit()

    def find_by_id(self, record_id):
        row = self.conn.execute(
            f"SELECT * FROM {self.table_name} WHERE id=?", (record_id,)
        ).fetchone()
        return dict(row) if row else None

    def find_all(self):
        rows = self.conn.execute(f"SELECT * FROM {self.table_name}").fetchall()
        return [dict(r) for r in rows]

    def find_by_field(self, key, value):
        if key not in self.columns:
            return []
        rows = self.conn.execute(
            f"SELECT * FROM {self.table_name} WHERE {key} LIKE ?", (f"%{value}%",)
        ).fetchall()
        return [dict(r) for r in rows]

    def search(self, query):
        conditions = [f"{c} LIKE ?" for c in self.columns]
        vals = [f"%{query}%" for _ in self.columns]
        where = " OR ".join(conditions)
        rows = self.conn.execute(
            f"SELECT * FROM {self.table_name} WHERE {where}", vals
        ).fetchall()
        return [dict(r) for r in rows]

    def count(self):
        return self.conn.execute(f"SELECT COUNT(*) FROM {self.table_name}").fetchone()[0]

    def close(self):
        self.conn.close()
