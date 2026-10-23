"""Exercise the real sketch helper on a host; no Arduino hardware required."""
import pathlib
import subprocess
import tempfile
import unittest


class ServoSweepTest(unittest.TestCase):
    def test_sweep_reaches_expected_angles_without_reversing(self):
        sketch = (pathlib.Path(__file__).resolve().parents[1] / "AIOR.ino").read_text()
        start = sketch.index("void sweepServo(")
        end = sketch.index("// -------------------- Ultrasonic", start)
        helper = sketch[start:end]
        harness = r"""
#include <vector>
#include <stdexcept>
#include <iostream>
struct ServoStub {
    std::vector<int> angles;
    void write(int angle) {
        if (angle < 0 || angle > 180 || angles.size() > 100) {
            throw std::runtime_error("sweep moved outside servo limits or did not terminate");
        }
        angles.push_back(angle);
    }
} scanServo;
void delay(int) {}
""" + helper + r"""
void check(int from, int to, int step, const std::vector<int>& expected) {
    scanServo.angles.clear();
    sweepServo(from, to, step);
    if (scanServo.angles != expected) throw std::runtime_error("unexpected sweep path");
}
int main() {
    try {
        std::vector<int> up, down, center;
        for (int a = 70; a <= 140; a += 5) up.push_back(a);
        for (int a = 140; a >= 0; a -= 5) down.push_back(a);
        for (int a = 0; a <= 70; a += 5) center.push_back(a);
        check(70, 140, 5, up);
        check(140, 0, -5, down); // Actual startup and obstacle-scan call.
        check(140, 0, 5, down);
        check(70, 140, -5, up);
        check(0, 70, 5, center);
        check(70, 70, 5, {70});
        check(70, 140, 0, {});
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
"""
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / "sweep.cpp"
            binary = pathlib.Path(folder) / "sweep"
            source.write_text(harness)
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
