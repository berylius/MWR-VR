# Height and arm measurement

Options > VR > Calibration > Measure height now opens a dedicated page. Holding
X for three seconds outside menus opens the same page. Release both triggers,
stand upright with arms straight out at shoulder height, then hold both triggers
until the gauge fills (two seconds). The page shows the saved height and actual
shoulder-to-wrist length after success. Back cancels; Measure again starts over.

Only fresh, focused, tracked input with a valid floor height and a T-pose advances
the gauge. Releasing either trigger, leaving the pose or losing tracking resets
partial progress. No settings change before completion. Samples are averaged
over the hold. Admission uses the physical arm span and its midpoint rather than
the original recenter axes, so turning before measuring does not reject a T-pose.

The saved values are `vr_playerHeight`, `vr_shoulderDown`,
`vr_shoulderHalfWidth`, `vr_measuredArmLength` (metres) and `vr_armScale`.
Shoulder width is estimated from height, as before. If no native arm model has
been drawn yet, the real arm length is saved immediately and its model scale is
fitted once the hands appear. Camera eye height and world scale do not change.

`src/client/resources/ui_scripts/height-tpose.svg` is the artwork source.
Native LUI does not load SVG materials; `tools/build_height_silhouette.py` converts
the SVG's circle/rectangle/polygon union into merged native white-image spans in
`height_tpose.lua`, embedded in the executable. Regenerate after editing the SVG;
`python tools/build_height_silhouette.py --check` checks the generated file.

Diagnostics: `vr_height_status` includes measurement state, progress, floor height,
last arm span and deferred fitting. `vr_measure_height` opens the page;
`vr_measure_height cancel` cancels. `begin` arms without opening (menu-owned).
