# ParamLab App Specification

## Purpose

`ParamLab` is the validation app for `orbit12ui`.

It should contain at least one parameter for every supported type and key option variant.

## Required Coverage

1. `int` basic range/step/default
2. `float` default mode
3. `float` percent mode (including out-of-0..1 values)
4. `boolean` with `truefalse`
5. `boolean` with `onoff`
6. `enum` with wrapping enabled
7. `enum` with wrapping disabled
8. `string` editor flow with `✓`, `←`, `✗`, `A-Z`, `0-9`
9. `rate` with bars + triplets
10. `rate` without bars
11. `rate` without triplets
12. `note` with octaves disabled
13. `note` with octaves enabled and custom `octaveRange` (including negative lower bound)
14. `button` action item
15. `folder` containing nested params
16. at least one `viscondition` example

## Success Criteria

1. All listed parameter types render in one consistent list UI.
2. Label/value layout remains stable (left/right).
3. Edit mode switches encoder behavior from navigation to value editing.
4. String entry supports approve/delete/cancel and wrapping symbol set.
5. Conditional visibility updates correctly when source parameter changes.
