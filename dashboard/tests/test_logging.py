"""
Checks for saving readings to logs/: file layout, blank cells for missing
values, New run and size splits, and that a failing disk never stops the
dashboard. Uses temporary folders, so nothing is left in logs/.

    python -m unittest discover -s tests -v
"""

import builtins
import csv
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import app as dashboard  # noqa: E402


class ReadingLogTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        self.printed = []
        self._print = builtins.print
        builtins.print = lambda *a, **k: self.printed.append(" ".join(str(x) for x in a))

    def tearDown(self):
        builtins.print = self._print
        shutil.rmtree(self.folder, ignore_errors=True)

    def files(self):
        return sorted(os.listdir(self.folder))

    def rows(self, name):
        with open(os.path.join(self.folder, name), newline="") as f:
            return list(csv.reader(f))

    def test_no_file_until_the_first_reading(self):
        log = dashboard.ReadingLog(self.folder)
        self.assertEqual(self.files(), [])
        log.write({"pitch": 1.0})
        log.close()
        self.assertEqual(len(self.files()), 1)

    def test_same_columns_as_the_page_export_and_blank_cells(self):
        log = dashboard.ReadingLog(self.folder)
        log.write({"pitch": 1.25, "roll": -2.0, "power": None})
        log.write({"power": 0.0, "pitch_target": 8.0, "roll_target": -4.0})
        log.close()
        rows = self.rows(self.files()[0])
        self.assertEqual(rows[0], ["time_iso", "elapsed_s", "run", "pitch_deg", "roll_deg",
                                   "power_w", "pitch_target_deg", "roll_target_deg"])
        self.assertEqual(rows[1][2:], ["1", "1.25", "-2.0", "", "", ""])
        self.assertEqual(rows[2][2:], ["1", "", "", "0.0", "8.0", "-4.0"])
        self.assertTrue(rows[1][0].endswith("Z"))

    def test_new_run_starts_a_new_file_with_that_run_number(self):
        log = dashboard.ReadingLog(self.folder)
        log.write({"pitch": 1.0})
        log.new_run(2)
        log.write({"pitch": 2.0})
        log.close()
        names = self.files()
        self.assertEqual(len(names), 2)
        second = [n for n in names if "_run2" in n][0]
        self.assertEqual(self.rows(second)[1][2], "2")

    def test_big_file_rolls_over_to_a_new_part(self):
        log = dashboard.ReadingLog(self.folder, max_bytes=300)
        for i in range(20):
            log.write({"pitch": float(i), "roll": float(i), "power": 1.0})
        log.close()
        names = self.files()
        self.assertGreater(len(names), 1)
        self.assertTrue(any("_part2" in n for n in names))
        total_rows = sum(len(self.rows(n)) - 1 for n in names)
        self.assertEqual(total_rows, 20)  # nothing lost across the split

    def test_write_failure_stops_logging_but_never_raises(self):
        blocker = os.path.join(self.folder, "not_a_folder")
        open(blocker, "w").close()  # a file where the logs folder should be
        log = dashboard.ReadingLog(os.path.join(blocker, "logs"))
        log.write({"pitch": 1.0})
        log.write({"pitch": 2.0})
        self.assertFalse(log.enabled)
        warnings = [p for p in self.printed if "Stopped saving readings" in p]
        self.assertEqual(len(warnings), 1)

    def test_emit_reading_writes_to_the_log(self):
        log = dashboard.ReadingLog(self.folder)
        saved_log, saved_emit = dashboard.reading_log, dashboard.socketio.emit
        dashboard.reading_log = log
        dashboard.socketio.emit = lambda *a, **k: None
        try:
            dashboard.process_reading('{"pitch": 3.5, "roll": 1.0}')
            dashboard.process_reading("not json")
        finally:
            dashboard.reading_log, dashboard.socketio.emit = saved_log, saved_emit
            log.close()
        rows = self.rows(self.files()[0])
        self.assertEqual(len(rows), 2)  # header plus the one good reading
        self.assertEqual(rows[1][3:5], ["3.5", "1.0"])

    def test_flush_makes_the_last_rows_visible(self):
        log = dashboard.ReadingLog(self.folder)
        log.write({"pitch": 1.0})
        log.write({"pitch": 2.0})  # both inside the same second, so still buffered
        log.flush()
        self.assertEqual(len(self.rows(self.files()[0])), 3)  # header plus both rows
        log.close()

    def test_folder_size(self):
        with open(os.path.join(self.folder, "a.csv"), "w") as f:
            f.write("x" * 1000)
        self.assertEqual(dashboard.folder_size(self.folder), 1000)
        self.assertEqual(dashboard.folder_size(os.path.join(self.folder, "missing")), 0)


if __name__ == "__main__":
    unittest.main()
