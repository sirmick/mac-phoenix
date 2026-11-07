require './generator'

class ExecutorGenerator < Generator
    def self.filter_key
        "executor"
    end

    def initialize
        super
        @need_guest = false
        @noguest_types = Set.new(["void"])
        @expand_common = false
    end

    def need_guest
        if @need_guest then
            return yield
        else
            @need_guest = true
            result = yield
            @need_guest = false
            return result
        end
    end

    def type_needs_guest?(type)
        size_of_type(type) != 1 && ! @noguest_types.include?(type)
    end

    def guestify_type(type, guest)
        type.strip!
        return type, "", "" if not type_needs_guest?(type)
        if type =~ /^(.*)(\[[^\[\]]*\])$/ then
            post = $2
            type1, _, _ = guestify_type($1, true)
            
            #if guest then
            #    return "GUEST<#{type1}#{post}>", "", ""
            #else
                return type1, "", post
            #end
        elsif type =~ /^(.*)\*$/ then
            type1, _, _ = guestify_type($1, true)

            if guest then
                return "GUEST<#{type1}*>", "", ""
            else
                return type1, "*", ""
            end
        elsif guest then
            if type =~ /^const[ \t]+(.*)$/ then
                if type_needs_guest?($1) then
                    return "const GUEST<#{$1}>"
                else
                    return type
                end
            else
                return "GUEST<#{type}>"
            end
        else
            return type, "", ""
        end
    end

    def decl(type, thing=nil)
        t1, pre, post = guestify_type(type, @need_guest)
        
        return "#{t1} #{pre}#{thing}#{post}"
    end

    def convert_expression(expr)
        return expr.gsub(/\'([^\']+)\'/) {|x| "\"#{$1}\"_4"}
    end

    def declare_struct_union(what, value)
        @noguest_types << value["name"]
        
        @out << "#{what} #{value["name"]}"
        if value["members"] then
            @out << "{"
            @out << "GUEST_STRUCT;"
            need_guest { declare_members(value["members"]) }
            @out << "}"
        end
        @out << ";"
        if value["size"] then
            @out << "static_assert(sizeof(#{value["name"]}) == #{value["size"]});"
        end
        # Incomplete types (forward declarations only) have nothing to describe.
        if value["members"] then
            @out << "void describeStruct(const #{value["name"]}&, std::ostream&);"
        end
    end

    # Emit logging::logField calls describing each leaf member, expanding
    # `common` macros and recursing into anonymous nested struct/union members.
    def describe_members(out, members, access, label)
        members.each do |member|
            if member["common"] then
                describe_members(out, $global_name_map[member["common"]]["common"]["members"],
                                 access, label)
            elsif member["struct"] or member["union"] then
                sub = member["struct"] || member["union"]
                describe_members(out, sub, "#{access}#{member["name"]}.",
                                 "#{label}#{member["name"]}.")
            else
                out << "    logging::logField(os, \"#{label}#{member["name"]}\", "
                out << "#{access}#{member["name"]});\n"
            end
        end
    end

    def describe_struct(out, value)
        out << "void describeStruct(const #{value["name"]}& x, std::ostream& os)\n"
        out << "{\n"
        out << "    os << \"#{value["name"]}{\";\n"
        describe_members(out, value["members"] || [], "x.", "")
        out << "    os << \"}\";\n"
        out << "}\n"
    end

    # Net effect of the preprocessor conditionals opened/closed by a verbatim
    # block, so structs declared inside an `#if` (e.g. mc68000-only) are skipped.
    def preprocessor_depth_delta(text)
        delta = 0
        text.each_line do |line|
            s = line.strip
            if s =~ /^#\s*(if|ifdef|ifndef)\b/
                delta += 1
            elsif s =~ /^#\s*endif\b/
                delta -= 1
            end
        end
        delta
    end

    # Struct/union definitions to describe for a header, de-duplicated by name
    # (an opaque forward declaration can be followed by the full definition) and
    # skipping types declared inside verbatim `#if` blocks (e.g. mc68000-only).
    # Returns [ordered names, name => richest value].
    def struct_definitions(header)
        defs = {}
        order = []
        depth = 0
        header.data.each do |item|
            key, value = main_elem(item)
            if key == "verbatim"
                depth += preprocessor_depth_delta(value)
                next
            end
            next if depth > 0
            next unless key == "struct" or key == "union"
            next if item["api"] == "carbon"
            # Only complete types (with members) can be described/sized.
            next if (value["members"] || []).empty?
            name = value["name"]
            if !defs.key?(name)
                defs[name] = value
                order << name
            elsif (value["members"] || []).size > (defs[name]["members"] || []).size
                defs[name] = value
            end
        end
        [order, defs]
    end

    def generate_structdump(header, module_name, best)
        out = ""
        out << "// Generated by make-multiverse.rb - DO NOT EDIT\n"
        out << "#include <base/structdump.h>\n"
        out << "#include <base/logging.h>\n"
        out << "#include <#{module_name}.h>\n\n"

        order, defs = struct_definitions(header)
        # A type defined in more than one module is described only where it has
        # the fullest definition.
        emitted = order.select { |name| best[name][0] == module_name }

        out << "namespace Executor\n{\n"
        emitted.each do |name|
            describe_struct(out, defs[name])
            out << "\n"
        end
        out << "} // namespace Executor\n\n"

        # Registration table for the type registry (used by the cxmon `dt`
        # command).  Kept in namespace structdump so the generated names resolve
        # the types and describeStruct in the enclosing Executor namespace.
        out << "namespace Executor { namespace structdump {\n"
        out << "namespace\n{\n"
        emitted.each do |name|
            out << "void dump_#{name}(std::ostream& os, const void* p)\n"
            out << "{\n"
            out << "    describeStruct(*(const #{name}*)p, os);\n"
            out << "}\n"
        end
        out << "}\n\n"
        out << "void registerStructDumps_#{module_name}()\n"
        out << "{\n"
        unless emitted.empty?
            out << "    static const TypeDesc types[] = {\n"
            emitted.each do |name|
                out << "        { \"#{name}\", sizeof(#{name}), &dump_#{name} },\n"
            end
            out << "    };\n"
            out << "    registerTypes(types, sizeof(types) / sizeof(types[0]));\n"
        end
        out << "}\n"
        out << "}} // namespace Executor::structdump\n"
        out
    end

    # The single translation unit that references every per-module registration
    # function, so none of the generated structdump objects are stripped by the
    # linker.
    def generate_reference_all_struct_dumps(modules)
        out = "// Generated by make-multiverse.rb - DO NOT EDIT\n"
        out << "#include <base/structdump.h>\n\n"
        out << "namespace Executor { namespace structdump {\n"
        modules.each { |m| out << "void registerStructDumps_#{m}();\n" }
        out << "\nvoid registerAllStructDumps()\n{\n"
        modules.each { |m| out << "    registerStructDumps_#{m}();\n" }
        out << "}\n}}\n"
        out
    end
    def declare_dispatcher(value, extern:false)
        @available_dispatchers << value["name"]
        # A component type's calls (selector-location: Component) go through
        # _ComponentDispatch's own dispatcher; nothing to declare.
        return if value["selector-location"] == "Component"
        @out << "EXTERN_" if extern
        @out << "DISPATCHER_TRAP(#{value["name"]}, "
        @out << "#{hexlit(value["trap"])}, #{value["selector-location"]});\n"
    end
    def declare_lowmem(value)
        @out << "const LowMemGlobal<#{decl(value["type"])}> #{value["name"]}"
        @out << "{ #{hexlit(value["address"],12)} };\n"
        # @out << "LOWMEM_ACCESSOR(#{value["name"]});\n"
        if value["type"] =~ /^(.*)\[[^\[\]]*\]$/ then
            # MacPhoenix: an array's accessor gives its address, as Apple's
            # do (LMGetCurApName, LMGetSysResName: a StringPtr), exported
            # from InterfaceLib like the others.
            ptype = decl($1 + "*")
            @out << "inline #{ptype} LMGet#{value["name"]}() { return ptr_from_longint<#{ptype}>(#{hexlit(value["address"])}); }\n"
            lowmem_export(value, "LMGet#{value["name"]}")
        else
            sz = size_of_type(value["type"])

            if value["accessor"] == "pointer" then
                # MacPhoenix: Apple's accessors for the queue headers give
                # their address (QHdrPtr LMGetEventQueue(void)); the setter
                # copies one in.
                ptype = decl(value["type"] + "*")
                @out << "inline #{ptype} LMGet#{value["name"]}() { return &LM(#{value["name"]}); }\n"
                @out << "inline void LMSet#{value["name"]}(#{decl("const " + value["type"]+"*", "val")}) { LM(#{value["name"]}) = *val; }\n"
            elsif !sz || sz > 4 then
                @out << "inline void LMGet#{value["name"]}(#{decl(value["type"]+"*", "val")}) { *val = LM(#{value["name"]}); }\n"
                @out << "inline void LMSet#{value["name"]}(#{decl("const " + value["type"]+"*", "val")}) { LM(#{value["name"]}) = *val; }\n"
            else
                @out << "inline #{decl(value["type"])} LMGet#{value["name"]}() { return LM(#{value["name"]}); }\n"
                @out << "inline void LMSet#{value["name"]}(#{decl(value["type"],"val")}) { LM(#{value["name"]}) = val; }\n"
            end
            lowmem_export(value, "LMGet#{value["name"]}")
            lowmem_export(value, "LMSet#{value["name"]}")
    end

    end

    # MacPhoenix: a library's exported variable: Executor's getter
    # (`executor:`, the name; true: data_NAME) gives its guest-addressable
    # storage, asked for once the libraries are set up (builtinlibs).
    def declare_data(value)
        return unless value["executor"]
        getter = value["executor"] == true ? "data_#{value["name"]}" : value["executor"]
        @out << "void *#{getter}();"
        Array(value["library"]).each do |lib|
            @out << " DATA_EXPORT(#{lib.gsub(/\W/, '_')}, \"#{lib}\", #{value["name"]}, #{getter});"
        end
        @out << "\n"
    end

    # MacPhoenix: a low-memory accessor's export: InterfaceLib's, or the
    # library the definition names (`library:` PrivateInterfaceLib: the
    # System's private globals, ExpandMem).
    def lowmem_export(value, accessor)
        if value["library"] then
            @out << "LIBRARY_FUNCTION(#{value["library"]}, #{accessor}, #{accessor});\n"
        else
            @out << "NOTRAP_FUNCTION2(#{accessor});\n"
        end
    end

    def handle_regcall_conv(fun)
        args = (fun["args"] or [])
        @out << (fun["returnreg"] or "void")
        argregs = args.map do |arg|
            basetype = $1 if arg["type"] =~ /^(.*)\* *$/
            if arg["register"] =~ /^Out<(.*)>$/ then
                "Out<#{basetype}, #{$1}>"
            elsif arg["register"] =~ /^InOut<(.*)>$/ then
                "InOut<#{basetype}, #{$1}>"
            else
                arg["register"]
            end
        end
        @out << "(" << argregs.join(", ") << ")"
        @out << ", " << fun["executor_extras"] if fun["executor_extras"]
    end

    # The C strings and Points of InterfaceLib's lowercase glue (base/cglue.h).
    CGLUE_STRING_IN = ["ConstStringPtr", "ConstStr255Param", "ConstStr63Param",
                       "ConstStr32Param", "ConstStr31Param", "ConstStr27Param", "ConstStr15Param"]
    CGLUE_STRING_OUT = ["StringPtr", "Str255", "Str63", "Str32", "Str31", "Str27", "Str15"]
    CGLUE_POINT = ["Point", "Cell"]

    # MacPhoenix: one of InterfaceLib's lowercase C routines (numtostring,
    # drawstring, ptinrect), for PowerPC and CFM-68K code: the Pascal routine
    # `cglue:` names, defined in the same file. Its input strings are C
    # strings, its output strings are made C strings, a Point is passed by
    # address. `cstring-in:`/`cstring-inout:` name string arguments that the
    # routine reads although they are typed as outputs (DIZero's volume
    # name; UpperString's string). Not exported while the Pascal routine
    # isn't implemented.
    def declare_cglue(fun)
        name = fun["name"]
        item = $global_name_map[fun["cglue"]]
        target = item && item["function"]
        raise "#{name}: cglue: #{fun["cglue"]} is not a function" unless target
        return unless target["executor"]
        tname = target["name"]
        cname = tname
        if target["executor"].is_a?(String) then
            cname = target["executor"] =~ /_$/ ? target["executor"] + tname : target["executor"]
        end
        ins = Array(fun["cstring-in"])
        inouts = Array(fun["cstring-inout"])
        params = []
        call = []
        before = []
        after = []
        (target["args"] or []).each_with_index do |arg, i|
            n = arg["name"] || "arg#{i}"
            t = arg["type"].strip
            if inouts.include?(n) then
                params << "char *#{n}"
                before << "cglue::c2p(#{n});"
                call << "(StringPtr)#{n}"
                after << "cglue::p2c(#{n});"
            elsif CGLUE_STRING_IN.include?(t) or ins.include?(n) then
                params << "const char *#{n}"
                call << "cglue::CString(#{n})"
            elsif CGLUE_STRING_OUT.include?(t) then
                params << "char *#{n}"
                call << "(StringPtr)#{n}"
                after << "cglue::p2c(#{n});"
            elsif CGLUE_POINT.include?(t) then
                params << decl("#{t}*", n)
                call << "*#{n}"
            else
                params << decl(t, n)
                call << n
            end
        end
        ret = (target["return"] or "void")
        @out << "inline #{ret} cglue_#{name}(#{params.join(", ")}) {"
        @out << before.join
        invoke = "#{cname}(#{call.join(", ")})"
        if ret == "void" then
            @out << "#{invoke};" << after.join
        elsif after.empty? then
            @out << "return #{invoke};"
        else
            @out << "#{ret} ret = #{invoke};" << after.join << "return ret;"
        end
        @out << "}\n"
        @out << "LIBRARY_FUNCTION(#{Array(fun["library"] || "InterfaceLib")[0]}, #{name}, cglue_#{name});\n"
    end

    def declare_function(fun)
        if fun["cglue"] then
            declare_cglue(fun)
            return
        end

        if fun["dispatcher"] && ! @available_dispatchers.include?(fun["dispatcher"]) then
            disp = $global_name_map[fun["dispatcher"]]
            if disp and disp["dispatcher"] then
                declare_dispatcher(disp["dispatcher"], extern:true)
            end
        end

        name = fun["name"]
        dispatcher = (fun["dispatcher"] && $global_name_map[fun["dispatcher"]]["dispatcher"])
        trap = (fun["trap"] or (dispatcher and dispatcher["trap"]))

        if dispatcher and dispatcher["selector-location"] == "Component" then
            # A call into a component (docs/executor/LIBRARIES.md): the
            # component's own code answers it. A library that exports it
            # (AppleScriptLib) gets an entry that makes the call: the
            # parameters after the instance, in Pascal sizes.
            if fun["library"] then
                sizes = (fun["args"] or [])[1..-1].map {|arg| pascal_size(arg["type"])}.join
                @out << "COMPONENT_CALL(#{Array(fun["library"])[0]}, #{name}, #{hexlit(fun["selector"])}, \"#{sizes}\");\n"
            end
            # A call Executor's own (native) component of this type
            # answers: registered with the type's dispatch by selector,
            # its arguments read from the call's parameter block.
            if fun["executor"] then
                cname = fun["executor"].is_a?(String) ?
                    (fun["executor"] =~ /_$/ ? fun["executor"] + name : fun["executor"]) : name
                args = (fun["args"] or [])
                @out << (fun["return"] or "void") << " " << cname
                @out << "(" << (args.map {|arg| decl(arg["type"], arg["name"])}).join(", ") << ");"
                @out << "COMPONENT_FUNCTION(#{fun["dispatcher"]}, #{name}, #{hexlit(fun["selector"])}, #{cname});\n"
            end
            return
        end

        cname = name
        if fun["executor"] then
            if fun["executor"].is_a?(String) then
                if fun["executor"] =~ /_$/ then
                    cname = fun["executor"] + name
                else
                    cname = fun["executor"]
                end
            end    
        end
        two = name == cname ? "2" : ""

        args = (fun["args"] or [])

        if fun["aslm"] and not fun["library"] then
            # A routine of an ASLM function set that PowerPC code binds
            # through ASLM itself (LibraryManager.yaml): registered with
            # Executor's PowerPC binder by set and index.
            set = fun["aslm"]["set"]
            index = fun["aslm"]["index"]
            if fun["raw"] then
                @out << "ASLM_RAW(\"#{set}\", #{index}, #{name});\n"
            else
                @out << (fun["return"] or "void") << " " << cname
                @out << "(" << (args.map {|arg| decl(arg["type"], arg["name"])}).join(", ") << ");"
                @out << "ASLM_FUNCTION(\"#{set}\", #{index}, #{name}, #{cname});\n"
            end
            return
        end

        if fun["library"] and not trap then
            # A shared library's export (docs/executor/LIBRARIES.md): reached
            # by name from PowerPC and CFM-68K code, not through a trap.
            # A raw one reads its own arguments from the PowerPC registers
            # and is registered by its library's code; here it is only
            # declared to be there (checked when the library is set up).
            # A list: the first library has the export, the others export
            # the same routine too (ObjectSupportLib's glue, InterfaceLib's
            # on CFM-68K).
            libs = Array(fun["library"])
            lib = libs[0]
            libs[1..-1].each {|other| @out << "LIBRARY_ALSO(#{other}, #{name}, #{lib});\n" }
            if fun["aslm"] or fun["ot"] then
                # Open Transport's: bound through the Shared Library Manager
                # on 68k as well (`ot: native`: an OT 1.1.1 addition no 68k
                # function set has, PowerPC only); the routine reads its
                # arguments through ot::Call, so only their sizes matter
                # here: a Boolean, a word (a byte is a word too, the way
                # Pascal passes it), or a long.
                letter = lambda do |t|
                    if t.nil? || t == "void" then "V"
                    elsif t == "Boolean" then "B"
                    elsif (size_of_type(t) || 4) <= 2 then "W"
                    else "L"
                    end
                end
                sig = args.map {|arg| letter.call(arg["type"])}.join
                pascal = fun["callconv"].to_s.downcase == "pascal"
                @out << "namespace ot { uint32_t #{cname}(const Call &); }"
                set, setname = fun["aslm"] ? [fun["aslm"]["set"], fun["aslm"]["name"]] : ["", name]
                @out << "OT_FUNCTION(#{lib}, #{name}, \"#{set}\", "
                @out << "\"#{setname}\", #{pascal}, \"#{sig}\", "
                @out << "'#{letter.call(fun["return"])}', #{cname});\n"
            elsif fun["raw"] then
                @out << "LIBRARY_RAW(#{lib}, #{name});\n"
            elsif not fun["executor"] then
                # Declared, not implemented yet (Appearance.yaml): not
                # exported, so an import of it stays unresolved and logged.
            else
                @out << (fun["return"] or "void") << " " << cname
                @out << "(" << (args.map {|arg| decl(arg["type"], arg["name"])}).join(", ") << ");"
                if lib =~ /\W/ then
                    # A library name that isn't an identifier ("Memory Manager").
                    @out << "LIBRARY_FUNCTION_NAMED(#{lib.gsub(/\W/, '_')}, \"#{lib}\", #{name}, #{cname});\n"
                else
                    @out << "LIBRARY_FUNCTION(#{lib}, #{name}, #{cname});\n"
                end
            end
            return
        end

        @out << (fun["return"] or "void") << " " << cname

        @out << "(" << (args.map {|arg| decl(arg["type"], arg["name"])}).join(", ") << ");"

        if fun["file_trap"] == "hfs" then
            trap_sel_disp = hexlit(Integer(trap) & 0xA0FF)
        else
            trap_sel_disp = hexlit(trap)
        end
        if fun["selector"] then
            sub = "SUB"
            trap_sel_disp += ", #{hexlit(fun["selector"])}, #{hexlit(fun["dispatcher"])}"
        else
            sub = ""
        end

        if not fun["executor"] then
        elsif fun["file_trap"] == "mfs" then
        elsif fun["file_trap"] == "hfs" then
            @out << "HFS_#{sub}TRAP(#{name.gsub(/^PBH/,"PB")}, #{name}, "
            @out << "#{fun["args"][0]["type"]}, #{trap_sel_disp});"
        elsif fun["file_trap"] then
            @out << "FILE_#{sub}TRAP(#{name}, #{fun["args"][0]["type"]}, #{trap_sel_disp});"
        elsif trap && (fun["executor_extras"] || fun["returnreg"] || args.any? {|arg| arg["register"]}) then
            if fun["variants"] && fun["variants"].include?(nil) then
                # A flag combination with no routine of its own (~ in the
                # definition): the trap and the other variants, one by one.
                variants = fun["variants"]
                nflags = variants.size >= 3 ? 2 : 1
                args1 = fun["args"][0..-(nflags + 1)]
                type = (fun["return"] or "void") + "(" + (args1.map {|arg| decl(arg["type"])}).join(", ") + ")"
                @out << "REGISTER_TRAP2_UNEXPORTED(#{cname}, #{hexlit(trap)}, "
                handle_regcall_conv(fun)
                @out << ");"
                variants.each_with_index do |variant, i|
                    next unless variant
                    flags = nflags == 2 ? [(i >> 1) & 1, i & 1] : [i]
                    @out << " TRAP_VARIANT(#{variant}, #{cname}, #{type}, #{flags.map {|f| f == 1 ? "true" : "false"}.join(", ")});"
                end
                @out << "\n"
            elsif fun["variants"] then
                variants = fun["variants"]
                nflagstr = variants.size >= 3 ? "2" : ""
                @out << "REGISTER_#{nflagstr}FLAG_TRAP(#{cname}, #{variants.join(", ")}, "
                @out << "#{hexlit(trap)}, "
                @out << (fun["return"] or "void")
                args1 = variants.size >= 3 ? fun["args"][0..-3] : fun["args"][0..-2]
                @out << "(" << (args1.map {|arg| decl(arg["type"])}).join(", ") << ")"
                @out << ", "
            else
                @out << "REGISTER_#{sub}TRAP#{two}(#{name}, #{trap_sel_disp}, "
            end
            unless fun["variants"] && fun["variants"].include?(nil)
                handle_regcall_conv(fun)
                @out << ");\n"
            end
        elsif fun["selector"] then
            # `export: false`: the trap's selector only, not an InterfaceLib
            # export (a private call Apple's headers don't name).
            @out << (fun["export"] == false ? "PASCAL_SUBTRAP_UNEXPORTED(" : "PASCAL_SUBTRAP(")
            @out << "#{name}, #{hexlit(trap)}, "
            @out << "#{hexlit(fun["selector"])}, #{hexlit(fun["dispatcher"])});\n"
        elsif trap then
            if name == cname then
                if args.size == 0 && !fun["return"] then
                    @out << "REGISTER_TRAP2(#{name}, #{hexlit(trap)}, void());\n"
                else
                end
            else
                # `export: false`: the trap only; InterfaceLib's export is
                # another definition's routine (GetItemStyle's).
                @out << (fun["export"] == false ? "PASCAL_TRAP_UNEXPORTED(" : "PASCAL_TRAP(")
                @out << "#{name}, #{hexlit(trap)});\n"
            end
        else
            @out << "NOTRAP_FUNCTION#{two}(#{name});\n"
        end
        # A trap's routine that a library other than InterfaceLib exports
        # for PowerPC and CFM-68K code (SoundLib's Sound Manager calls): the
        # same entry under that library's name too.
        if fun["library"] and trap and fun["executor"] then
            Array(fun["library"]).each {|lib| @out << "LIBRARY_ALSO(#{lib}, #{name}, InterfaceLib);\n" }
        end
        # A selector the guest's own code answers (the Drag Manager's),
        # exported by a library: glue through the dispatcher's table entry.
        if fun["library"] and fun["selector"] and fun["dispatcher"] and not fun["executor"] then
            Array(fun["library"]).each do |lib|
                @out << "TRAP_GLUE(#{lib}, #{name}, #{fun["dispatcher"]}, #{hexlit(fun["selector"])});\n"
            end
        end
    end

    # Bytes a Pascal caller pushes for an argument of this type: a word for
    # anything up to two bytes, a long otherwise.
    def pascal_size(type)
        sz = size_of_type(type) || 4
        sz <= 2 ? 2 : 4
    end

    def declare_funptr(value)
        name = value["name"]
        args = (value["args"] or [])

        @out << "using #{name} = UPP<"
        @out << (value["return"] or "void") << "("
        args = (value["args"] or [])
        @out << args.map {|arg|decl(arg["type"], arg["name"])}.join(", ")
        @out << ")"
        if value["returnreg"] || value["executor_extras"] || args.any? {|arg| arg["register"]} then
            @out << ", Register<"
            handle_regcall_conv(value)
            @out << ">"
        elsif value["callconv"] == "C" then
            @out << ", callconv::CCall"
        end
        @out << ">;\n"
    end

    def declare_verbatim(value)
        @out << "BEGIN_EXECUTOR_ONLY\n"
        @out << value.strip << "\n"
        @out << "END_EXECUTOR_ONLY\n"
    end

    def remap_name(name)
        name == "MacTypes" ? "ExMacTypes" : name
    end
     
    def generate_preamble(header)
        super
        @out << "#pragma once\n"
        #if header.name == "MacTypes" then
        @out << "#include \"base/mactype.h\"\n"
        @out << "#include <cassert>\n"
        @out << "#include <iosfwd>\n"
        @out << "#include <base/lowglobals.h>\n"
        #end

        header.included.each do |file|
            @out << "#include \"#{remap_name(file)}.h\"\n"
        end
        @out << "\n"

        @out << "#define MODULE_NAME #{header.name}\n"
        @out << "#include <base/api-module.h>\n"

        @out << "\n\n"

        @out << "namespace Executor {\n\n"
    end

    def generate_postamble(header)
        @out << "} /* namespace Executor */"
    end

    def generate_header(header)
        @current_header = header
        @available_dispatchers = Set.new
        super
    end

    def make_api_ifdef(api)
        if api == "carbon" then
        else
            yield
        end
    end

    def generate(defs)
        print "Writing Headers...\n"
        FileUtils.mkdir_p "#{$options.output_dir}/api"
        FileUtils.mkdir_p "#{$options.output_dir}/trap_instances"
        FileUtils.mkdir_p "#{$options.output_dir}/structdump"

        # A type can be defined in more than one module (e.g. an opaque QElem in
        # MacTypes and the full union in OSUtil); describe it only where it has
        # the fullest definition.
        best = {}
        defs.topsort.each do |name|
            name2 = remap_name(name)
            order, defs_here = struct_definitions(defs.headers[name])
            order.each do |tname|
                count = (defs_here[tname]["members"] || []).size
                best[tname] = [name2, count] if !best.key?(tname) || count > best[tname][1]
            end
        end

        moduleList = []
        defs.topsort.each do |name|
            name2 = remap_name(name)
            moduleList << name2
            formatted_file "#{$options.output_dir}/api/#{name2}.h" do |f|
                f << generate_header(defs.headers[name])
            end
            formatted_file "#{$options.output_dir}/structdump/#{name2}.cpp" do |f|
                f << generate_structdump(defs.headers[name], name2, best)
            end
            formatted_file "#{$options.output_dir}/trap_instances/#{name2}.cpp" do |f|
                f << <<END
#define INSTANTIATE_TRAPS_#{name2}
#include <#{name2}.h>

// Function for preventing the linker from considering the static constructors in this module unused
namespace Executor::ReferenceTraps {
    void #{name2}() {}
}
END
            end
        end
        formatted_file "#{$options.output_dir}/trap_instances/ReferenceAllTraps.cpp" do |f|
            f << "namespace Executor {\n"
            f << "namespace ReferenceTraps {\n"
            moduleList.each { |m| f << "void #{m}();\n" }
            f << "}\nvoid ReferenceAllTraps() {\n"
            moduleList.each { |m| f << "ReferenceTraps::#{m}();\n" }
            f << "}\n}\n"
        end
        formatted_file "#{$options.output_dir}/structdump/ReferenceAllStructDumps.cpp" do |f|
            f << generate_reference_all_struct_dumps(moduleList)
        end
        File.open "#{$options.output_dir}/trap_instances/trap_instances.cmake", "w" do |f|
            f << "set(trap_instance_sources\n"
            f << "    \"#{$options.output_dir}/trap_instances/ReferenceAllTraps.cpp\"\n"
            moduleList.each { |m| f << "    \"#{$options.output_dir}/trap_instances/#{m}.cpp\"\n" }
            f << ")\n"
        end
        print "Done.\n"
    end
end