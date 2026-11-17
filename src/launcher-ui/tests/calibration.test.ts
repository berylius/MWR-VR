import assert from "node:assert/strict";
import test from "node:test";
import { angleModePatch, calibrationBank } from "../src/calibration";
import { makeDraft, visible, type SettingsState } from "../src/model";

test("independent angles seed once and retain both banks and physical hands", () => {
  const state: SettingsState = { ok: true, values: {}, defaults: {}, limits: {}, choices: {},
    controllerPresets: [], onboarding: { gameAvailable: true, hasVRConfig: true },
    build: { configuration: "Release", optimized: true } };
  for (const mode of ["standard", "legacy"]) {
    const bank = calibrationBank({ vr_controllerPoseMode: mode });
    state.values[bank.independent] = false; state.values[bank.initialized] = false;
    for (const [axis, key] of bank.shared.entries()) {
      state.values[key] = (axis + 1) * (mode === "standard" ? 5 : -7);
      state.limits[key] = { min: -180, max: 180, step: 1, displayScale: 1 };
    }
    for (const hand of bank.hands) for (const key of hand) {
      state.values[key] = 0; state.limits[key] = { min: -180, max: 180, step: 1, displayScale: 1 };
    }
  }
  state.values.vr_controllerPoseMode = "standard";
  let draft = makeDraft(state.values, state.limits);
  const bank = calibrationBank(draft);
  const seeded = angleModePatch(draft, state, true);
  assert.equal(seeded[bank.hands[0][1]], 10);
  assert.equal(seeded[bank.hands[1][2]], 15);
  assert.equal(seeded[bank.initialized], true);
  draft = { ...draft, ...makeDraft(seeded, state.limits), [bank.hands[0][1]]: "37" };
  draft = { ...draft, ...makeDraft(angleModePatch(draft, state, false), state.limits) };
  const again = angleModePatch(draft, state, true);
  assert.equal(again[bank.hands[0][1]], undefined);
  assert.equal(draft[bank.hands[0][1]], "37");
  assert.equal(draft[bank.hands[1][1]], "10");
  const legacy = calibrationBank({ ...draft, vr_controllerPoseMode: "legacy" });
  const other = angleModePatch({ ...draft, vr_controllerPoseMode: "legacy" }, state, true);
  assert.equal(other[legacy.hands[0][1]], -14);
  assert.equal(draft[bank.hands[0][1]], "37");
  assert.equal(visible({ name: "left", category: "calibration", group: "angles", label: "left", type: "number",
    visibleWhen: ["vr_controllerPoseMode", "standard"], visibleWhenAll: [[bank.independent, true]] },
    { ...draft, [bank.independent]: true }), true);
});
