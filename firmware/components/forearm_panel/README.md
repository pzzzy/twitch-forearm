# JC3248W535 panel adaptation

The panel-only driver derives from the manufacturer's demo snapshot mirrored at
`NorthernMan54/JC3248W535EN`, commit `56161e8269dbb2c32296dcc6e44d4a00f1a2b97e`,
under `JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL`.

- `esp_lcd_axs15231b.c` and its header retain Espressif Apache-2.0 notices.
- `jc3248_init.c` retains the Espressif CC0-1.0 notice from `esp_bsp.c` and its
  board-specific register sequence.
- Touch code is excluded from this panel component. The board adapter reads the
  documented eight-byte AXS15231B touch packet directly using I2C.

Changes to the panel driver: removed its touch dependency/implementation; fixed
the display-on boolean to follow ESP-IDF semantics; propagate column-window and
color-transfer errors. The vendor's QSPI RAMWR/RAMWRC continuation protocol is
preserved. Draw only complete ordered frames; this adaptation does not qualify
arbitrary partial rectangles.

Source hashes and schematic provenance are recorded in `hardware/board.json`.

License texts are retained alongside this component: `LICENSE.Apache-2.0` for
the adapted panel driver and header, and `LICENSE.CC0-1.0` for the initialization
table. `LICENSE.Mirror-MIT` preserves the pinned mirror's separate repository
notice for any mirror-level contributions; it does not replace the explicit
vendor-file license headers. Original native wrappers and host tests use the
project's Apache-2.0 license. See the root `THIRD_PARTY_NOTICES.md` for dependency
and firmware-binary distribution scope.

The board adapter has a reversible `INTERNAL_STRIP_RENDER` switch, currently
false after on-device comparison. In the internal-strip experiment, LVGL draws logical dirty areas into the first 48-row
internal DMA buffer. Each packed logical area is copied into its absolute
position in the persistent PSRAM framebuffer. Only the final logical flush sends
the whole completed frame to the panel. At that point both DMA buffers are
phase-reused for transport; the logical strip has already been copied, and LVGL
waits for flush completion before drawing again. This adds no SRAM allocation.

Setting the switch false restores the direct-mode baseline: LVGL draws only
invalidated areas at absolute positions in the full-sized PSRAM buffer. Nonfinal
logical flushes do not send panel data in either mode. Both composition modes
retain ordered full-frame QSPI transfers without arbitrary partial LCD updates.

The relevant pinned LVGL semantics are in `src/core/lv_refr.c`: `refr_area`
uses a full-screen buffer area and an invalid-area clip in direct mode;
`draw_buf_flush` marks the last area/part before calling the driver. A single
buffer is not swapped or synchronized with a second buffer.

Run `python3 tests/run_host.py` after resolving the project's managed LVGL
dependency. It uses the actual LVGL sources and checks both modes: two disjoint
dirty areas produce one complete transmitted frame, unchanged pixels persist,
logical strip rows use the correct packed stride, and overwriting the draw strip
after the final copy does not corrupt later rendering. These are host composition
checks. Both modes were also exercised on the attached panel; direct mode was
retained because internal-strip composition was slower for long chat text.
See the project verification record for measured results and limits.

Physical touch is an event stream, rather than a pressed/not-pressed boolean for
every I2C poll. The connected unit returned repeated nonzero-header `0x4b` idle
packets between valid Down/Contact/Up events. Treating those packets as releases,
and treating explicit Up as pressed, created extra contacts and reopened the
message under the Back button. `axs_touch_state.h` retains state across no-data
and invalid reports, releases only on explicit Up/zero-point reports, and cancels
without a click after three consecutive transport failures. Its regression test
uses the actual observed Up/idle packets and verifies one contact per event
sequence. Sampling continues independently of LVGL drawing.
The user confirmed that Back from a long message returns cleanly after this fix.
