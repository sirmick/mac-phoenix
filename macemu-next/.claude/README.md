# Claude Project Context

This folder contains context files that are automatically loaded when Claude works on the macemu-next project.

---

## Files

### 🚨 [debugging_guide.md](debugging_guide.md)
**ESSENTIAL DEBUGGING REFERENCE** - Read this first!

Contains:
- Quick reference card of most common commands
- Step-by-step debugging workflows
- Output format explanations
- Troubleshooting solutions
- Tips and tricks

**When to use**: ANY debugging session, especially boot failures or backend comparison.

### [context.md](context.md)
**Automatically loaded** into every Claude session.

Contains:
- Project overview and status
- Architecture summary (Platform API, backends, memory)
- Recent achievements with commit hashes
- Current investigation (timer interrupt timing)
- Essential commands and environment variables
- File organization
- Key technical points

**Purpose**: Give Claude immediate understanding of the project without requiring manual explanation.

### [instructions.md](instructions.md)
**Custom instructions** for Claude's behavior on this project.

Contains:
- 🚨 **CRITICAL debugging workflows at the top**
- Common command patterns and environment variables
- Unicorn-first development guidelines
- Platform API abstraction rules
- Validation requirements
- Documentation standards
- Code reading priority
- Common patterns to follow
- Response style guidelines

**Purpose**: Ensure Claude gives consistent, project-appropriate responses.

---

## What Gets Auto-Loaded

When you start a Claude session in this project, Claude automatically knows:

✅ **Project Status**
- Phase 1 complete (514k instruction validation)
- Phase 2 current (boot to desktop)
- Unicorn is primary backend, UAE is legacy, DualCPU is validator

✅ **Architecture**
- Platform API (`g_platform`) is the abstraction layer
- Three backends with clear roles
- Direct addressing memory system
- Hook architecture (UC_HOOK_BLOCK, UC_HOOK_INSN_INVALID)

✅ **Current Focus**
- Timer interrupt timing analysis
- Understanding why Unicorn stops at ~200k vs UAE 250k
- Functional testing approach

✅ **Essential Commands**
- How to build (`meson setup build && meson compile -C build`)
- How to run (`CPU_BACKEND=unicorn ./build/macemu-next ~/quadra.rom`)
- How to validate (`CPU_BACKEND=dualcpu ...`)
- How to trace and debug

✅ **File Locations**
- Where to find Platform API (`src/common/include/platform.h`)
- Where Unicorn backend lives (`src/cpu/cpu_unicorn.cpp`)
- Where docs are organized (`docs/Architecture.md`, `docs/deepdive/`)

---

## Benefits

### For You
- Don't have to explain project context every time
- Claude understands Unicorn-first focus automatically
- Consistent responses across sessions

### For Claude
- Immediate project understanding
- Clear guidelines on what to prioritize
- Knows where to look for information

### For the Project
- Consistent development approach
- Proper validation procedures followed
- Documentation standards maintained

---

## Updating Context

### When to Update

Update `context.md` when:
- Project phase changes (e.g., Phase 2 complete → Phase 3 starts)
- Major achievements (e.g., boot to desktop working)
- New critical issues discovered
- Significant architecture changes

Update `instructions.md` when:
- Development guidelines change
- New patterns emerge
- Response style needs adjustment

### How to Update

1. Edit the relevant file (`context.md` or `instructions.md`)
2. Keep it concise (Claude has context limits)
3. Update "Last Updated" date
4. Commit changes with descriptive message

---

## Example Session Flow

**Without .claude/context.md:**
```
User: "Can you help with the Unicorn backend?"
Claude: "Sure, what's the Unicorn backend?"
User: "It's a CPU backend for macemu-next..."
[5 minutes of explanation]
```

**With .claude/context.md:**
```
User: "Can you help with the Unicorn backend?"
Claude: "Sure! The Unicorn backend is our primary focus (src/cpu/cpu_unicorn.cpp).
        It's the JIT-compiled M68K CPU that's the future of macemu-next.
        What specifically do you need help with?"
[Claude already understands the project]
```

---

## Technical Details

### File Format
- Standard Markdown (.md)
- Organized with clear sections
- Links to relevant docs
- Code examples included

### Size Considerations
- Keep concise (Claude has token limits)
- Link to detailed docs instead of duplicating
- Prioritize essential information

### Loading Behavior
Claude loads these files at session start and keeps them in context throughout the session.

---

## Maintenance

### Regular Reviews
Review these files:
- After major milestones (e.g., Phase completion)
- Monthly for status updates
- When project direction changes

### What to Include
✅ Essential project info (status, architecture, goals)
✅ Current focus and blockers
✅ Common commands and patterns
✅ Critical technical points

❌ Detailed implementation (link to docs instead)
❌ Complete history (use completed/ docs)
❌ Exhaustive command reference (use Commands.md)

---

## Related Documentation

- [../docs/Architecture.md](../docs/Architecture.md) - Comprehensive architecture
- [../docs/ProjectGoals.md](../docs/ProjectGoals.md) - Vision and roadmap
- [../docs/TodoStatus.md](../docs/TodoStatus.md) - Current checklist
- [../docs/Commands.md](../docs/Commands.md) - Complete command reference

The .claude/ folder provides Claude with project context automatically.
The docs/ folder is for human-readable comprehensive documentation.

---

**Created**: January 3, 2026
**Purpose**: Auto-load project context into Claude sessions
**Benefit**: Skip repetitive explanations, get to productive work faster
