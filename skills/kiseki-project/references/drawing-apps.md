# Drawing and canvas operation

Use the [operating guide](operating.md) for target discovery and mode selection. Drawing adds app state: visible canvas bounds, chosen tool, foreground color/opacity, and brush or shape settings.

## Prepare the current canvas

Discover the target and capture it with the operation family's screenshot command. Use UIA/AX for toolbar names, values and controls; inspect pixels for the canvas, selected color and rendered marks. Prepare missing tool/color state within the user's requested workflow instead of ending the task at "not verified". Preserve requested colors, geometry, precision and timing; a high-contrast test color is appropriate only for an owned diagnostic canvas.

Record the image-to-action transform. Current-session input uses screen coordinates (macOS global points); selected-window messages use client coordinates; Xvfb uses its isolated display; CUA uses provider window-local coordinates. Native window bounds, client origins and full-window screenshots can differ. On Retina, apply the supplied scale. Subtracting a window origin alone is valid only when both inputs use the same units and origin convention.

## Execute a stroke

Use `input drag` for a continuous native path or `input sequence` when modifier/button state spans multiple actions. Both preserve requested points/timing and handle scoped cleanup. A path file accepts `x y` or `x y time_ms`; select button/modifiers through the documented options. See [native input](../../../docs/native-input.md).

```bash
"$kiseki_exe" input drag --file "$run_dir/points.txt" --backend system
```

For an established CUA workflow use `background cua draw`, retaining the requested provider. Its current `--max-segments` setting may reject a dense path. Read the error and current help; preserve the required detail and use an explicit suitable segment setting rather than silently thinning the path or changing to foreground input. Choose timing from task requirements and observed app behavior; do not use a small fixed cap as a reliability workaround.

## Verify and recover

Capture the same target after the stroke. Compare the intended region and shape against the requested result, not merely any pixel difference. A blank result calls for checking tool, color/opacity, canvas bounds, coordinate transform, focus/recipient, and delivery evidence. Fix observed app state and retry a small recoverable action within scope. Do not blindly redraw a whole stroke when the previous one may have partially appeared.

When target acceptance is the remaining issue, report the tested app/backend and the exact outcome. Keep before/after images and the path file. Note whether the current pointer/focus moved when this is part of the task; an overlay cursor is separate from the real pointer.
