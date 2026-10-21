# Spec: private ResourceDispatch selectors of System 7.5.5

`_ResourceDispatch` (`$A822`) selects on D0's low byte (signed); the high
byte is the parameter size in words. Besides the documented selectors
(`ReadPartialResource` 1, `WritePartialResource` 2, `SetResourceSize` 3,
`GetNextFOND` 10), the Finder and Apple's Help Manager call four that
Inside Macintosh doesn't document. Their names are ours. multiversal
already declares them (`multiversal/defs/ResourceMgr.yaml`, dispatcher
`ResourceDispatch`); implement the `C_` functions in
`romlib/res/resPrivate.cpp`.

Terms: a *resource map* is the handle Executor keeps per open resource
file (`resmaphand`); the maps form a chain from `TopMapHndl` through each
map's `nextmap` link; `SysMapHndl` is the System file's map and `SysMap`
its reference number. A map's *attribute byte* is the file's resource map
attributes (Executor's `resfatr`): Inside Macintosh documents bits 7
(read-only), 6 (compact) and 5 (changed); the low bits are private.
ResErr is set through Executor's `ROMlib_setreserr`.

## Selector 0: `Handle GetResourceMapHandle(INTEGER refNum)`

The resource map of an open resource file.

* `refNum` 0 means the System file (`SysMap`).
* If a file with that reference number is open: return its map, ResErr
  `noErr`.
* Otherwise: return nil, ResErr `resFNotFound` (-193).

## Selector 8: `SignedByte GetResourceMapPrivateFlags(Handle map)`

* `map` nil: return 0, ResErr `nilHandleErr` (-109).
* Otherwise: return the map's attribute byte with only bits 1-4 kept
  (attribute AND `$1E`), ResErr `noErr`.

## Selector 6: `Handle GetOverrideMap(Handle map)`

Bit 1 (`$02`) of a map's attribute byte marks an *override* map: a map
placed directly above another (its base) in the chain, so its resources
are found first. Several override maps may sit in a row above one base.

* ResErr is `noErr` unless the map isn't found.
* Walk the chain from `TopMapHndl`. Track the current *run* of consecutive
  override maps: the first override map met since the last non-override
  map (a non-override map ends the run).
* On reaching `map`: if a run is in progress (the maps directly above
  `map` are override maps), return the first map of that run, the
  topmost of them; otherwise return `map` itself.
* If `map` isn't in the chain: return nil, ResErr `resFNotFound`.

Examples, chain listed from the top:

| Chain (O = override) | GetOverrideMap(B) |
|---|---|
| A, B | B |
| O1, B | O1 |
| O1, O2, B | O1 |
| O1, A, O2, B | O2 |
| A, O1, C (B not in chain) | nil, resFNotFound |

## Selector -1 (`$02FF`): `Boolean ResourceMapIsSystem(Handle map)`

Whether a resource map belongs to the system: the Help Manager asks it of
the file a balloon's resource came from.

* `map` nil: return false, ResErr `resFNotFound`.
* Otherwise ResErr `noErr`, and return true when `map` is the System file's
  map (`SysMapHndl`), or the map of a file opened in system mode
  (Executor tracks those: `ROMlib_system_resource_file_p(refnum)`,
  `rsys/process.h`); false otherwise.

## Tests to write

gtests in `src/executor/tests` (resources.cpp has fixtures for opening
resource files): each selector's normal and error cases, ResErr after
each, and the GetOverrideMap table above (set the override bit on maps of
test files you open and arrange). Call them as the trap does, through
`_ResourceDispatch` with D0 set, as well as directly. All `executor`
ctests must pass.
