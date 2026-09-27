// Tests for the per-watermark selection filter used by "生成所选".
#include "watermark/WatermarkSelection.h"
#include <cassert>
#include <iostream>

namespace pdfmark {

void testWatermarkSelection() {
    std::cout << "[RUN] testWatermarkSelection\n";

    // Default-constructed configs are selected and have non-empty text.
    WatermarkConfig def;
    assert(def.selected);
    assert(filterSelectedWatermarks({def}).size() == 1);

    WatermarkConfig a; a.text = "A"; a.selected = true;
    WatermarkConfig b; b.text = "B"; b.selected = false;   // unchecked -> skipped
    WatermarkConfig c; c.text = "";  c.selected = true;    // empty text -> skipped
    WatermarkConfig d; d.text = "D"; d.selected = true;

    std::vector<WatermarkConfig> all{a, b, c, d};
    auto sel = filterSelectedWatermarks(all);
    assert(sel.size() == 2);
    assert(sel[0].text == "A");
    assert(sel[1].text == "D");

    // Nothing checked -> nothing generated.
    for (auto& cfg : all) cfg.selected = false;
    assert(filterSelectedWatermarks(all).empty());

    // Empty input is handled.
    assert(filterSelectedWatermarks({}).empty());

    std::cout << "[PASS] testWatermarkSelection\n";
}

} // namespace pdfmark
