# Collection Manager: behavioural specification

A specification of the QuickDraw GX Collection Manager, written from Apple's
documentation only (*Inside Macintosh: QuickDraw GX Environment and
Utilities*, chapter 5, "Collection Manager", 7 JUL 1996), for a from-scratch
implementation in Executor. Clean room: no Apple source code or disassembly
was used. Quotes are exact (from the archive pages listed at the end). Points
the documentation does not settle are marked **UNSPECIFIED**: settle them
with a test on a real Mac (Mac OS 7.5.5+ with the Collection Manager present,
or 8.x/9.x, where it is in the System).

**Settled since** (2026-10-28) by CollectionTest (tests/guest/collections)
on Apple's Collection Manager in Mac OS 9.0.4 on the G3: items are in tag
order and ID (signed) order within a tag, indexes from 1; adding an
existing item replaces its data and keeps its attributes; a size given to
the Get routines caps the bytes copied; the Hdl routines resize the handle;
`CopyCollection` empties the target and copies the default attributes;
the flattened format is 'flac' (Collections.r), read through each item's
offset. What Executor does: the comment at the top of
executor/core/romlib/collections.cpp.

The pages contain no trap numbers or selectors. The function order in
section 2 is the one this project uses (selector order); the docs do not
confirm it.

Result codes named in the docs:

| Name | Value | Doc text |
|---|---|---|
| `memFullErr` | -108 | "Can't allocate memory." |
| `resNotFound` | -192 | "Resource not found." |
| `collectionItemLockedErr` | -5750 | "Can't replace locked item." |
| `collectionItemNotFoundErr` | -5751 | "Can't locate item." |
| `collectionIndexRangeErr` | -5752 | "Index is out of range." |
| `collectionVersionErr` | -5753 | "Unrecognized version/data may be corrupt." |

Availability: Gestalt selector `gestaltCollectionMgrVersion` (its value is
not given on these pages); the example compares against `0x01008000`
"/* version 1.0f0 */".

General rule (p. 214): "When calling any of these functions, you must always
provide a valid collection object reference. If you do not, the behavior of
the function is undefined." No function has to check for a nil or bad
`Collection`.

---

## 1. Concepts

### 1.1 The collection object

`typedef struct PrivateCollectionRecord *Collection;` -- opaque; "The
contents of the collection object are private" (p. 208). It holds (p. 184):

- "an owner count, which reflects the current number of references to the collection"
- "an exception procedure, which you can use to handle errors that occur while operating on the collection"
- "default attributes"
- "a number of collection items"

"a collection is entirely memory-based" (p. 184). The collection is resized
automatically as items are added and removed.

### 1.2 Items

Each item has (p. 185):

- **tag** -- `typedef long CollectionTag; /* 4-byte identifier ('xxxx') */` (p. 209)
- **id** -- "a long value that, in conjunction with the collection tag, uniquely identifies the collection item"
- **attributes** -- "a set of 32 bit flags"
- **variable-length data** -- any size, including 0 (see AddCollectionItem).

Uniqueness: "in any collection there can be exactly one item with a
collection tag of 'tagA' and a collection ID of 10." IDs need not be
sequential and may be any long (examples use 0..9, 15, 20).

### 1.3 Three ways to name an item (p. 187)

1. **tag + id** -- unique and stable.
2. **tag + tag list position** -- "The tag list position of an item is the
   position of the item in the list of items with the same collection tag.
   Unlike a collection ID, the tag list positions of items with the same tag
   are sequential." 1-based: "A tag list position of 1 indicates the first
   item with the specified tag." (p. 243, p. 247). "the tag list position of
   an item can change if another item with the same collection tag is added
   to or removed from the collection."
3. **collection index** -- "A collection index uniquely identifies its item
   within a collection. Indexes across a collection do not have to be
   sequential. The collection index of any item in a collection can change if
   another item is added to or removed from the collection." Lookup by index
   is described as faster than by tag+id (p. 194, p. 198).

**Order within a tag list -- UNSPECIFIED, with conflicting evidence:**

- Figure 5-4 (p. 187): four 'tagA' items "have collection IDs 2, 6, 4, and 0"
  and "tag list positions 1, 2, 3, and 4" -- i.e. not sorted by id.
- The worked example (p. 193-195): 'GXPT' items with ids 0-9 are added in
  order, then id 20, then id 15. The text says adding id 15 "can change the
  tag list position of any other item with the 'GXPT' collection tag", and
  later, for tag list position 11: "the collection ID of this item turns out
  to be 15". With insertion order position 11 would be id 20; with ascending
  id order it is id 15. So the worked example implies **sorted by id**
  (ascending), and that adding an item can shift the positions of existing
  items with the same tag (which append-order would never do).
- Test: add ids 0..9, 20, 15 under one tag, read positions 11 and 12; also
  add ids in the order 2, 6, 4, 0, and with negative ids (signed sort?).

**Collection index -- UNSPECIFIED:** the docs never say it is 1-based, that
the valid range is 1..CountCollectionItems, or how indexes are ordered
(insertion order, or by tag then id). They say only "do not have to be
sequential" and that it "can change if another item is added to or removed
from the collection". No example iterates by index; the only iteration
example (Listing 5-18) goes tag by tag (GetIndexedCollectionTag from 1 to
CountCollectionTags) and tag list position by position. Test: index 0, 1,
count, count+1; order of indexes after adds of different tags/ids; index of
an item after removing/adding another.

**Order of the distinct tag list** (GetIndexedCollectionTag) -- UNSPECIFIED
(first-appearance order or sorted by tag value). Its positions are 1-based
("from 1 to the result of the CountCollectionTags function", p. 254).

### 1.4 Attributes (p. 186, 211-213)

32 bits, bit 31 the high bit.

| Bit(s) | Name | Meaning |
|---|---|---|
| 31 | `collectionLockBit` / `collectionLockMask` | "When an item has this attribute set, attempts to replace the item result in an error." |
| 30 | `collectionPersistenceBit` / `collectionPersistenceMask` | "When an item has this attribute set, the Collection Manager includes this item when flattening the collection. When this attribute is clear, the Collection Manager ignores the item when flattening the collection." |
| 16-29 | `collectionReserved0Bit`..`collectionReserved13Bit` | "reserved attributes" (Apple's) |
| 0-15 | `collectionUser0Bit`..`collectionUser15Bit` | "user attributes" -- the application's |

Masks: `noCollectionAttributes = 0x00000000`, `allCollectionAttributes =
0xFFFFFFFF`, `userCollectionAttributes = 0x0000FFFF`,
`defaultCollectionAttributes = 0x40000000` ("the persistent attribute is set
and all other attributes are clear"). `collectionXMask = 1L << collectionXBit`.

**Editing rule** (mask + values), used for item attributes, default
attributes, purging, partial flattening: "If you set the corresponding bit of
the attribute mask to 0, the Collection Manager leaves the attribute
unchanged from the original. ... If you set the corresponding bit in the
attribute mask to 1, the Collection Manager copies the new attribute value".
I.e. `attrs = (attrs & ~mask) | (values & mask)`. For matching (Purge,
FlattenPartial): an item matches when `(attrs & mask) == (values & mask)`
("If the values of all the specified attributes match", p. 234).

**Default attributes:** "A collection's default attributes determine the
initial attribute values assigned to items added to that collection" (p.
186). Initial value for NewCollection: "described by the
defaultCollectionAttributes constant, in which only the persistence attribute
is set" (p. 190), i.e. `0x40000000`. Default attributes are copied by
CopyCollection. Default attributes of a collection made by GetNewCollection
or after UnflattenCollection: UNSPECIFIED (is the default-attributes word
part of the flattened stream?).

**Lock rules** (all quoted):

- Replace: "You cannot replace a collection item if its lock attribute is
  set" (p. 198); AddCollectionItem / ReplaceIndexedCollectionItem / the Hdl
  variants return `collectionItemLockedErr`, "and the Collection Manager does
  not replace the item".
- Remove: "You can remove a collection item even if its lock attribute is
  set--the lock attribute only affects replacing." (p. 199). Also
  PurgeCollectionTag ("removes locked and unlocked items"), EmptyCollection
  ("even the locked items", p. 199), CopyCollection into an existing target
  ("removes all of the items in the second collection--including locked
  items", p. 191).
- PurgeCollection: no special case for locked items; they go if they match.
  "To avoid purging locked items, you should clear the lock attribute in the
  whichAttributes and matchingAttributes parameters." (p. 234). (Read
  literally that sentence is wrong -- clearing the lock bit in the mask means
  the lock bit is not tested; the way to spare locked items is lock bit set
  in the mask and clear in the values. Either way: Purge does not protect
  locked items by itself.)
- Changing attributes of a locked item (SetCollectionItemInfo &co.): no
  error is documented and "the lock attribute only affects replacing", so it
  is allowed (that is how an item gets unlocked). Confirm by test.

### 1.5 Owner count, clone, copy, dispose

- New collection: "The new collection contains no items and has an owner
  count of 1." (p. 216)
- CloneCollection increments the count and returns the same reference.
- DisposeCollection "decrements the owner count ... If the resulting owner
  count is 0, this function releases the memory occupied by the collection
  object".
- CopyCollection makes a distinct object with owner count 1 (nil target) or
  fills an existing one; see section 2.

### 1.6 Exception procedures (p. 184, 205, 222-224, 270)

`OSErr MyExceptionProc(Collection target, OSErr whichErr);` (Listing 5-27
declares it `pascal OSErr`; the reference prototype on p. 270 has no
`pascal`. Calling convention: UNSPECIFIED by the reference page; the example
uses `pascal`.)

- When called: "whenever the Collection Manager is operating on that
  collection object and an error occurs, the Collection Manager calls this
  function, sending it a reference to the collection for which the error
  occurred and the result code associated with the error." Only "if you
  installed one" (p. 184).
- Return value: "You can change the error from one result code to another by
  returning as the function result the new result code." / "You can handle
  the error and return the noErr error code, which indicates that the
  Collection Manager should return control to the place in your application
  that generated the error, as if no error had occurred." Also allowed:
  `longjmp` out of it.
- So the function's result is the exception procedure's result. The example
  (p. 205): a `collectionItemLockedErr` from AddCollectionItem turned into
  `noErr`, the caller carries on; a `collectionItemNotFoundErr` from
  RemoveCollectionItem is raised.
- UNSPECIFIED: the initial exception procedure of a new collection (presumably
  nil = none); whether it is called for errors from functions that return no
  OSErr (NewCollection, CopyCollection, Purge*, GetNewCollection), for errors
  returned by a flatten proc, and for `memFullErr`; whether the item is
  changed when an error is turned into `noErr` (the docs say only "as if no
  error had occurred"; for the locked case the item is presumably not
  replaced). CopyCollection does not copy it (target keeps its own); CloneCollection
  shares it (same object).

### 1.7 Persistence and flattening

Only persistent items (bit 30) are flattened, by both FlattenCollection and
FlattenPartialCollection. CopyCollection copies "all of the information"
(not only persistent items). Format of the flattened stream: not described
(section 3).

---

## 2. Functions

### NewCollection
`Collection NewCollection(void);`
- Result: "A reference to the newly created collection object."
- "The new collection contains no items and has an owner count of 1."
- Default attributes `defaultCollectionAttributes` (0x40000000) (p. 190).
- "The NewCollection function does not return an error code; it returns nil
  if it cannot create a new collection object."
- Exception procedure initially: UNSPECIFIED (presumably none).

### DisposeCollection
`void DisposeCollection(Collection target);`
- "decrements the owner count of the collection object referenced by the
  target parameter. If the resulting owner count is 0, this function releases
  the memory occupied by the collection object, and the collection object
  reference contained in the target parameter becomes invalid."
- "The behavior of this function is undefined if you do not provide a
  reference to a valid collection object in the target parameter."

### CloneCollection
`Collection CloneCollection(Collection target);`
- "increments the owner count of the collection object referenced by the
  target parameter, and, as a programming convenience, returns a reference to
  this collection as the function result." Result "is effectively a copy of
  the reference you provide in the target parameter."
- "The CloneCollection function does not return an error code."

### CountCollectionOwners
`long CountCollectionOwners(Collection source);`
- "returns as its function result the owner count of the collection object
  referenced by the source parameter."

### CopyCollection
`Collection CopyCollection(Collection source, Collection target);`
- target: "A reference to a collection object to contain the copied
  collection items. You may provide nil for this parameter to request that
  the Collection Manager create a new collection object".
- "copies all of the information (except the owner count and exception
  procedure) from the collection object referenced by the source parameter
  into the collection object referenced by the target parameter."
- nil target: "this function creates a new collection object to copy the
  information into. (This function does not return an error code; it returns
  nil if it cannot create a new collection object.)" New object has "an owner
  count of 1" (p. 191); it does "not increment the owner count of the
  aCollection collection".
- Existing target (p. 191): the function "removes all of the items in the
  second collection--including locked items--before copying the items from
  the first collection into the second collection" and "copies the default
  attribute values from the first collection into the second collection";
  "it leaves the owner count and the exception procedure of the second
  collection unchanged."
- "In either case, this function returns a reference to the collection
  object containing the copied information." (so: the target when non-nil).
- Items copied with their attributes (persistent or not): "all of the
  information". Whether indexes/tag positions come out in the same order:
  UNSPECIFIED (follows from the ordering rule).
- Out of memory with an existing target (partial copy? nil result?):
  UNSPECIFIED. source == target: UNSPECIFIED.

### GetCollectionDefaultAttributes
`long GetCollectionDefaultAttributes(Collection source);`
- Result: "A long word containing the bit flags that make up the
  collection's default attributes."

### SetCollectionDefaultAttributes
`void SetCollectionDefaultAttributes(Collection target, long whichAttributes, long newAttributes);`
- whichAttributes: "A mask indicating which bit flags in the target
  collection's default attributes you want to alter." newAttributes: "the new
  values for the bit flags."
- "If the bit is set, this function copies the value of the corresponding bit
  from the newAttributes parameter into the corresponding bit of the default
  attributes ... If the bit is not set, the corresponding bit ... remains
  unchanged."
- Affects only items added afterwards ("From then on, when you added an item
  to the collection, the new item would have its lock and persistence
  attributes set", p. 186). Reserved bits may be set through it (the example
  sets the lock bit with `allCollectionAttributes`).

### CountCollectionItems
`long CountCollectionItems(Collection source);`
- "returns as its function result the total number of items in the
  collection". Counts all items (example: 15 = 12 + 3, p. 193).

### AddCollectionItem
`OSErr AddCollectionItem(Collection target, CollectionTag tag, long id, long itemSize, void *itemData);`
- itemSize: "The size in bytes of the item's variable-length data." (input
  only, by value). itemData: "A pointer to the item's variable-length data."
- New item contains "the collection tag specified by the tag parameter", "the
  collection ID specified by the id parameter", "the attributes specified by
  the default attributes of the target collection", "the variable-length data
  specified by the itemSize and itemData parameters".
- Data is copied: "after calling this function, you may alter this
  information or free the memory pointed to by this parameter without
  affecting the collection."
- Existing tag+id: "this function removes the original item and replaces it
  with the new one, unless the existing item is locked. If it is locked, this
  function returns a collectionItemLockedErr result code." Replace may change
  the size (p. 198: "The new item does not have to be the same size as the
  replaced item").
- **Attributes on replace: UNSPECIFIED.** The wording "removes the original
  item and replaces it with the new one" plus "the new item contains ... the
  attributes specified by the default attributes" suggests the attributes are
  reset to the collection's defaults; but AddCollectionItemHdl's page says it
  "removes the variable-length data from the original item and replaces it
  with the new data", which suggests attributes are kept. Test both.
  Whether the replaced item keeps its collection index / tag list position:
  UNSPECIFIED.
- Size 0 / nil data: "If you specify 0 for this parameter, or provide nil for
  the itemData parameter, this function copies no information into the
  variable-length data of the new item, or removes the variable-length data
  if the item already exists." (Item exists with data size 0.) Negative
  itemSize: UNSPECIFIED.
- Errors: `memFullErr` (-108), `collectionItemLockedErr` (-5750).

### GetCollectionItem
`OSErr GetCollectionItem(Collection source, CollectionTag tag, long id, long *itemSize, void *itemData);`
- **itemSize is in/out.** In: "A pointer to a long value indicating the
  number of bytes of data you want returned in the itemData parameter." Out:
  "On return, this value indicates the size in bytes of the variable-length
  data associated with the specified item." (the item's full size, not the
  number of bytes copied).
- Bytes copied: "the number of bytes returned in the data parameter is either
  the value specified by the size parameter or the actual number of bytes of
  the specified item's data, whichever is lower." (p. 200). "You may specify
  a value for the itemSize parameter that is greater than the actual number
  of bytes ... however, this function never returns in the itemData parameter
  more data than contained in the specified item's variable-length data."
- itemSize nil (`dontWantSize`): "indicate that you want to copy all the
  specified item's variable-length data and you do not want to determine the
  size of this data."
- itemData nil (`dontWantData`): no data copied; with a non-nil itemSize the
  call just returns the size (the documented two-call idiom). Whether the
  input value of `*itemSize` is read at all when itemData is nil: irrelevant
  (nothing copied); it is overwritten with the size.
- Both nil: allowed ("or neither, actually, although that doesn't prove to be
  very useful", p. 200) -- acts as an existence check.
- Negative `*itemSize` in: UNSPECIFIED. Bytes beyond the copied count in the
  buffer: untouched (presumably).
- Errors: `collectionItemNotFoundErr` (-5751). Outputs on error: UNSPECIFIED.

### RemoveCollectionItem
`OSErr RemoveCollectionItem(Collection target, CollectionTag tag, long id);`
- "removes the item specified by the tag and id parameters ... This function
  removes the specified item even if its lock attribute is set."
- Missing: "this function returns a collectionItemNotFoundErr result code."
- Errors: `collectionItemNotFoundErr` (-5751).

### SetCollectionItemInfo
`OSErr SetCollectionItemInfo(Collection target, CollectionTag tag, long id, long whichAttributes, long newAttributes);`
- "copies bit values from the newAttributes parameter to the attributes
  associated with the specified item", bits selected by whichAttributes (set:
  copied; clear: "remains unchanged").
- Only attributes: "You cannot, for instance, use these functions to change
  the index of an item, or the size of its variable-length data." (p. 197).
- Locked item: no error documented (see 1.4) -- allowed.
- Errors: `collectionItemNotFoundErr` (-5751).

### GetCollectionItemInfo
`OSErr GetCollectionItemInfo(Collection source, CollectionTag tag, long id, long *index, long *itemSize, long *attributes);`
- All three outputs optional (nil = `dontWantIndex`/`dontWantSize`/
  `dontWantAttributes`): index = "the collection index of the specified
  item"; itemSize = "the size in bytes of the variable-length data";
  attributes = "a copy of the attributes". itemSize here is output only.
- Errors: `collectionItemNotFoundErr` (-5751).

### ReplaceIndexedCollectionItem
`OSErr ReplaceIndexedCollectionItem(Collection target, long index, long itemSize, void *itemData);`
- No item with that index: "returns a collectionIndexRangeErr result code."
- Locked: "returns a collectionItemLockedErr result code" (not replaced).
- Otherwise the new item has "the same collection tag as the original item",
  "the same collection ID as the original item", "the same attributes as the
  original item", "the variable-length data specified by the itemSize and
  itemData parameters". **Attributes kept.**
- Data copied (caller's buffer free afterwards). Size 0 or nil data: "copies
  no information into the variable-length data of the new item, or removes
  the variable-length data if the item already exists."
- Whether the item keeps the same index afterwards: UNSPECIFIED (presumably).
- Errors: `memFullErr`, `collectionItemLockedErr`, `collectionIndexRangeErr`.
  Order of checks (range before lock) follows from the text.

### GetIndexedCollectionItem
`OSErr GetIndexedCollectionItem(Collection source, long index, long *itemSize, void *itemData);`
- Same itemSize/itemData semantics as GetCollectionItem (in/out size,
  min(requested, actual) bytes copied, nil size = copy all, nil data = size
  only).
- Errors: `collectionIndexRangeErr` (-5752).

### RemoveIndexedCollectionItem
`OSErr RemoveIndexedCollectionItem(Collection target, long index);`
- "removes the item specified by the index parameter ... This function
  removes the specified item even if its lock attribute is set."
- Missing: `collectionIndexRangeErr`.
- Errors: `collectionIndexRangeErr` (-5752).

### SetIndexedCollectionItemInfo
`OSErr SetIndexedCollectionItemInfo(Collection target, long index, long whichAttributes, long newAttributes);`
- As SetCollectionItemInfo, item by index.
- Errors: `collectionIndexRangeErr` (-5752).

### GetIndexedCollectionItemInfo
`OSErr GetIndexedCollectionItemInfo(Collection source, long index, CollectionTag *tag, long *id, long *itemSize, long *attributes);`
- All four outputs optional (nil = `dontWantTag`/`dontWantId`/`dontWantSize`/
  `dontWantAttributes`).
- Errors: `collectionIndexRangeErr` (-5752).

### CollectionTagExists
`Boolean CollectionTagExists(Collection source, CollectionTag tag);`
- "returns ... a Boolean value indicating whether any of the items in the
  collection ... contain the collection tag specified by the tag parameter."

### CountCollectionTags
`long CountCollectionTags(Collection source);`
- "the number of distinct collection tags contained by the items of the
  collection".

### GetIndexedCollectionTag
`OSErr GetIndexedCollectionTag(Collection source, long whichTag, CollectionTag *tag);`
- whichTag: "The position of the desired collection tag in the source
  collection's list of distinct collection tags." 1-based ("from 1 to the
  result of the CountCollectionTags function").
- tag: output, "the collection tag that lies at the specified position". nil
  tag: UNSPECIFIED (not documented as optional).
- Order of the list: UNSPECIFIED (see 1.3).
- Errors: `collectionIndexRangeErr` (-5752).

### CountTaggedCollectionItems
`long CountTaggedCollectionItems(Collection source, CollectionTag tag);`
- "the total number of items in the source collection whose collection tags
  match the value you specify". 0 for an absent tag (implied).

### GetTaggedCollectionItem
`OSErr GetTaggedCollectionItem(Collection source, CollectionTag tag, long position, long *itemSize, void *itemData);`
- position: "The tag list position associated with the specific item";
  1-based (p. 243).
- Same itemSize/itemData semantics as GetCollectionItem.
- Errors: `collectionIndexRangeErr` (-5752) -- also, presumably, for a tag
  with no items (UNSPECIFIED; only this code is listed).

### GetTaggedCollectionItemInfo
`OSErr GetTaggedCollectionItemInfo(Collection source, CollectionTag tag, long position, long *id, long *index, long *itemSize, void *attributes);`
- Note the order: id, **index**, itemSize, attributes (differs from
  GetCollectionItemInfo). `attributes` is typed `void *` in the prototype
  but described as "A pointer to a long value".
- All four outputs optional.
- Errors: `collectionIndexRangeErr` (-5752).

### PurgeCollection
`void PurgeCollection(Collection target, long whichAttributes, long matchingAttributes);`
- "removes from the target collection any items whose attributes match the
  criteria"; "compares the specified attributes of each item ... If the values
  of all the specified attributes match, the function removes the item."
- Locked items are removed when they match (see 1.4).
- whichAttributes == 0: every item matches (follows from the rule; confirm).

### PurgeCollectionTag
`void PurgeCollectionTag(Collection target, CollectionTag tag);`
- "removes from the target collection all items whose collection tag matches
  the value of the tag parameter. This function removes locked and unlocked
  items."

### EmptyCollection
`void EmptyCollection(Collection target);`
- "removes every item in the collection" (locked too, p. 199). Default
  attributes, exception procedure and owner count are not mentioned:
  presumably unchanged (UNSPECIFIED).

### FlattenCollection
`OSErr FlattenCollection(Collection source, CollectionFlattenProc flattenProc, void *refCon);`
- "repeatedly calls the callback function ... Each time it calls this
  function, it provides the callback function with a pointer to a block of
  memory containing flattened data. It continues to call this function until
  it has flattened the entire collection."
- refCon passed through unchanged to every call.
- "When flattening the source collection, this function includes only the
  collection items whose persistence attribute is set."
- "This function can return any error returned by the callback function."
  Whether flattening stops at the first callback error: UNSPECIFIED
  (presumably).
- No result codes listed beyond callback errors (memFullErr possible:
  UNSPECIFIED).

### FlattenPartialCollection
`OSErr FlattenPartialCollection(Collection source, CollectionFlattenProc flattenProc, void *refCon, long whichAttributes, long matchingAttributes);`
- "includes only the collection items whose attributes specified by the
  whichAttributes parameter match the values specified by the
  matchingAttributes parameter."
- And: "includes only the collection items whose persistence attribute is
  set, regardless of the values you provide in the whichAttributes and
  matchingAttributes parameters." So an item is written iff persistent AND
  matching.
- Callback errors returned as with FlattenCollection.

### UnflattenCollection
`OSErr UnflattenCollection(Collection target, CollectionFlattenProc flattenProc, void *refCon);`
- "unflattens a stream of bytes into the collection object you specify with
  the target parameter." Target is an existing collection (examples use a
  fresh NewCollection).
- "Each time it calls this function, it provides the callback function with a
  pointer to a block of memory and a requested size. The callback function is
  responsible for reading the next set of bytes from the flattened byte
  stream and copying the data into the block of memory." It keeps calling
  "until it has unflattened the entire collection" -- so the stream is
  self-delimiting (the reader must know when to stop from the data itself;
  Listing 5-25 notes that a caller reading from a file must know the total
  size beforehand only because of how it fills the handle).
- Errors: `memFullErr`, `collectionVersionErr` (-5753), plus any callback
  error.
- UNSPECIFIED: whether existing items in target are kept (merged; same
  tag+id replaced? locked?) or emptied first; whether the target's default
  attributes are restored from the stream; state of target after an error.

### GetCollectionExceptionProc
`CollectionExceptionProc GetCollectionExceptionProc(Collection source);`
- "returns ... a pointer to the exception procedure installed in the
  collection object". Value when none installed: UNSPECIFIED (presumably nil).

### SetCollectionExceptionProc
`void SetCollectionExceptionProc(Collection target, CollectionExceptionProc newExceptionProc);`
- "copies the function pointer from the newExceptionProc parameter into the
  collection object". nil to remove: UNSPECIFIED (presumably).

### GetNewCollection
`Collection GetNewCollection(short collectionID);`
- "searches the current resource file path for a collection ('cltn')
  resource with the resource ID specified by the collectionID parameter. If
  it finds such a resource, this function creates a new collection object,
  initializes it with the information stored in the resource, and returns a
  reference to it". Not found: "it returns nil".
- "You can use the MemError and ResError functions to check for other errors
  after calling this function." Result codes listed: `memFullErr`,
  `resNotFound`. (Listing 5-26 treats nil with `ResError() == noErr` as
  resNotFound, i.e. ResError may be noErr on a nil return.)
- Not compatible with flattened data: "you cannot recreate the collection
  from this resource using the GetNewCollection function. The resource format
  created by the FlattenCollectionToHdl and AddResource functions is
  incompatible with the resource format expected by the GetNewCollection
  function." (p. 203)
- Item attributes come from the resource (lock, persistence, reserved, user
  bits). Default attributes of the new collection: UNSPECIFIED (presumably
  `defaultCollectionAttributes`). Resource released afterwards or not,
  Get1Resource vs GetResource ("current resource file path" = GetResource):
  as stated. Duplicate tag+id in the resource: UNSPECIFIED.

### AddCollectionItemHdl
`OSErr AddCollectionItemHdl(Collection target, CollectionTag tag, long id, Handle itemData);`
- As AddCollectionItem with the data = the handle's contents; size = the
  handle's size (implied; "specifying the item's variable-length data using a
  handle rather than a pointer and a data size").
- Data copied; the handle is not taken over ("you may alter this information
  or free the memory referenced by this parameter without affecting the
  collection").
- Existing tag+id: "removes the variable-length data from the original item
  and replaces it with the new data, unless the existing item is locked. If
  it is locked, this function returns a collectionItemLockedErr result code."
  (Attribute handling vs AddCollectionItem: see the UNSPECIFIED note there.)
- nil handle: UNSPECIFIED (by analogy with AddCollectionItem: empty data).
- Errors: `memFullErr`, `collectionItemLockedErr`.

### GetCollectionItemHdl
`OSErr GetCollectionItemHdl(Collection source, CollectionTag tag, long id, Handle itemData);`
- "You must provide a valid Macintosh Memory Manager handle for this function
  to copy the data into." "If you provide a valid Macintosh Memory Manager
  handle in the itemData parameter, the function uses this parameter to
  return a copy of the variable-length data".
- itemData may be `dontWantData` (nil): no copy (existence check).
- Handle size: not stated; `memFullErr` is listed, implying the handle is
  resized to hold the data. Whether it is set to exactly the item size
  (shrinking a larger handle): UNSPECIFIED (presumably exact, as
  FlattenCollectionToHdl "resizes the handle as necessary"). Locked/purged
  handle: UNSPECIFIED.
- Errors: `memFullErr`, `collectionItemNotFoundErr`.

### ReplaceIndexedCollectionItemHdl
`OSErr ReplaceIndexedCollectionItemHdl(Collection target, long index, Handle itemData);`
- As ReplaceIndexedCollectionItem with the handle's contents: same tag, id,
  attributes; new data; data copied.
- Errors: `memFullErr`, `collectionItemLockedErr`, `collectionIndexRangeErr`.

### GetIndexedCollectionItemHdl
`OSErr GetIndexedCollectionItemHdl(Collection source, long index, Handle itemData);`
- As GetCollectionItemHdl, item by index. (The page does not mention nil
  itemData.)
- Errors as listed on the page: `memFullErr`, **`collectionItemNotFoundErr`**
  (not collectionIndexRangeErr as the other indexed functions). Which one a
  bad index really returns: UNSPECIFIED -- test.

### FlattenCollectionToHdl
`OSErr FlattenCollectionToHdl(Collection source, Handle flattened);`
- "flattens the collection ... into a block of memory referenced by the
  handle you provide in the flattened parameter." "You may specify a handle of
  size 0; this function resizes the handle as necessary to hold the flattened
  data."
- The "possible implementation" (Listing 5-20) writes from offset 0, growing
  the handle with `SetHandleSize(h, position + theSize)` per block: prior
  contents are overwritten and the final size is exactly the flattened
  length, whatever size the handle had. (Sample only; the real function's
  handling of a larger handle is UNSPECIFIED but presumably the same.)
  Persistent items only (it is FlattenCollection).
- Errors: `memFullErr`.

### UnflattenCollectionFromHdl
`OSErr UnflattenCollectionFromHdl(Collection target, Handle flattened);`
- "unflattens the information referenced by the handle ... and stores the
  unflattened collection in the collection object referenced by the target
  parameter." Valid collection and handle required.
- Handle not disposed (Listing 5-24/5-25 release/dispose it themselves).
- Errors: `memFullErr`, `collectionVersionErr`.

---

## 3. Formats and callbacks

### 3.1 Flattened stream

**Not described.** The docs give no byte layout for FlattenCollection's
output. The only hints: it carries a version that UnflattenCollection checks
(`collectionVersionErr`, "Unrecognized version/data may be corrupt"); it is
self-delimiting (unflatten reads until "it has unflattened the entire
collection"); it is not the 'cltn' format. For interchange with real Mac
data (GX print records, clipboard, files written by real apps) the layout
has to be determined from data a real Mac produces (flatten known
collections on a real Mac and compare), not from these docs.

### 3.2 The 'cltn' resource (p. 272, Listing 5-28)

```
type 'cltn' {
    longint = $$CountOf(ItemArray);
    array ItemArray {
        longint;                                   /* tag */
        longint;                                   /* id */
        boolean itemUnlocked = false,              /* defined attributes */
                itemLocked = true;
        boolean itemNonPersistent = false,
                itemPersistent = true;
        unsigned bitstring[14] = 0;                /* reserved attributes */
        unsigned bitstring[16] userBits;           /* user attributes */
        wstring;
        align word;
    };
};
```

Byte layout (Rez: big-endian; `boolean` is one bit; `wstring` is a 2-byte
length followed by that many bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | item count N |
| then, per item: | | |
| +0 | 4 | tag |
| +4 | 4 | id |
| +8 | 4 | attributes: bit 31 lock, bit 30 persistence, bits 29-16 reserved, bits 15-0 user (the order matches the attribute bit numbers, so this is the attribute long as is) |
| +12 | 2 | data length L |
| +14 | L | data |
| +14+L | 0 or 1 | pad to an even offset (`align word`, relative to the resource start) |

"a string containing the variable-length data for the item" -- the data is
the wstring's bytes (no terminator). (The Rez labels `itemUnlocked = false`
etc. are value names, not defaults: each item must state them.)

### 3.3 Flatten procedure (`CollectionFlattenProc`, p. 269)

`OSErr MyFlattenProc(long size, void *data, void *refCon);`

- Flattening: "it provides a pointer to the current block of flattened data
  in the data parameter and the size of the current block in the size
  parameter." The **Collection Manager owns the buffer**; the proc consumes
  `size` bytes from it.
- Unflattening: "The Collection Manager specifies the size of the requested
  block in the size parameter, and your function should read or copy the
  requested number of bytes of flattened data into the block of memory
  pointed to by the data parameter." Again the **Collection Manager supplies
  the buffer**; the proc fills exactly `size` bytes.
- refCon: "the same value you originally passed as the refCon parameter".
- Result: "If the execution of this function results in any fatal error, you
  should return the error code back to the Collection Manager as the function
  result. If the function executes successfully, you should return the noErr
  error code". Flatten/Unflatten "can return any error returned by the
  callback function".
- Block sizes and number of calls: UNSPECIFIED (any chunking; the Hdl
  examples work for any). Unflatten may request small header pieces first;
  short reads have no way to be signalled except an error. Listing 5-21's
  sample `UnflattenProc` is declared `void` (returns nothing) -- a doc slip.
- Calling convention (C vs pascal): not stated; the prototypes are C.

### 3.4 Exception procedure

See 1.6. `OSErr MyExceptionProc(Collection target, OSErr whichErr);`

---

## 4. Sources

https://developer.apple.com/library/archive/documentation/mac/Legacy/GXEnvironment/GXEnvironment-NNN.html
for NNN =

- 182 Collection Manager; 183 About the Collection Manager; 184 Collection
  Objects; 185 Collection Items; 186 Collection Attributes; 187 Methods of
  Identifying Collection Items; 188 Using the Collection Manager; 189
  Determining Whether the Collection Manager Is Available; 190 Creating or
  Disposing of a Collection; 191 Cloning or Copying a Collection; 192
  Changing the Default Attributes of a Collection; 193 Adding Items to a
  Collection; 194 Determining the Collection Index of an Item; 195
  Determining the Tag and ID of an Item; 196 Determining the Size of an
  Item's Variable-Length Data; 197 Getting and Setting the Attributes of an
  Item; 198 Replacing Items in a Collection; 199 Removing Items From a
  Collection; 200 Retrieving the Variable-Length Data From an Item; 201
  Examining the Collection Tags of a Collection; 202 Flattening and
  Unflattening a Collection; 203 Reading Collections From and Writing
  Collections to Disk; 204 Reading a Collection From a Collection Resource;
  205 Installing an Exception Procedure
- 207 Data Types; 208 Collection Objects; 209 Collection Tags; 210 Optional
  Return Value Constants; 211 Attributes Masks; 212 Attribute Bit Numbers;
  213 Attribute Bit Masks
- 214 Functions; 215-217 NewCollection, DisposeCollection; 218-221
  CloneCollection, CountCollectionOwners, CopyCollection; 222-224
  Get/SetCollectionExceptionProc; 225-227 Get/SetCollectionDefaultAttributes;
  228-230 AddCollectionItem, ReplaceIndexedCollectionItem; 231-236
  RemoveCollectionItem, RemoveIndexedCollectionItem, PurgeCollection,
  PurgeCollectionTag, EmptyCollection; 237-239 CountCollectionItems,
  CountTaggedCollectionItems; 240-243 GetCollectionItem,
  GetIndexedCollectionItem, GetTaggedCollectionItem; 244-247
  GetCollectionItemInfo, GetIndexedCollectionItemInfo,
  GetTaggedCollectionItemInfo; 248-250 SetCollectionItemInfo,
  SetIndexedCollectionItemInfo; 251-254 CollectionTagExists,
  CountCollectionTags, GetIndexedCollectionTag; 255-258 FlattenCollection,
  FlattenPartialCollection, UnflattenCollection; 259-265 AddCollectionItemHdl,
  ReplaceIndexedCollectionItemHdl, GetCollectionItemHdl,
  GetIndexedCollectionItemHdl, FlattenCollectionToHdl,
  UnflattenCollectionFromHdl; 266-267 GetNewCollection
- 268 Application-Defined Functions; 269 MyFlattenProc; 270 MyExceptionProc
- 271 Resources; 272 The Collection Resource
