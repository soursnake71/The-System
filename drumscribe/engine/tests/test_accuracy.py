"""Accuracy on a synthetic drummer (perfect stems with -26 dB bleed between them), against exact ground truth.

Run: python -m pytest tests -q        (or python -m tests.test_accuracy for the full table)
"""
import numpy as np
import pytest

from drumscribe import pipeline
from tests.synth import SR, evaluate, print_report, render


@pytest.mark.parametrize("seed", [1, 2])
def test_synthetic_kit(seed):
    stems, mix, truth, dur = render(seed=seed)
    flat, bpm = pipeline.transcribe_stems(stems, SR, dur)
    rep, tot = evaluate(truth, [(h.time, h.piece, h.velocity) for h in flat])
    print_report(rep, tot)
    assert tot["f"] >= 0.95, "hit detection + kit-piece F-measure"
    assert tot["mae_ms"] <= 0.6, "mean timing error (ms)"
    assert tot["p95_ms"] <= 2.5, "95% of hits within this many ms"
    for piece in ("kick", "snare", "hh_closed", "hh_open", "ride", "crash1", "crash2", "tom_himid", "tom_low"):
        assert rep[piece]["f"] >= 0.9, piece
    assert abs(bpm - 96.0) < 0.5


if __name__ == "__main__":
    for s in (1, 2, 3):
        stems, mix, truth, dur = render(seed=s)
        flat, bpm = pipeline.transcribe_stems(stems, SR, dur)
        print("seed", s, "tempo", bpm)
        print_report(*evaluate(truth, [(h.time, h.piece, h.velocity) for h in flat]))
