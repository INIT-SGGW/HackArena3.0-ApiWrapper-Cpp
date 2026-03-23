function(hackarena3_generate_grpc_sources out_srcs out_hdrs output_dir)
    set(proto_roots ${ARGN})
    set(proto_files)
    set(proto_path_args)

    foreach(proto_root IN LISTS proto_roots)
        list(APPEND proto_path_args "--proto_path=${proto_root}")
    endforeach()

    foreach(proto_root IN LISTS proto_roots)
        file(GLOB_RECURSE root_proto_files CONFIGURE_DEPENDS "${proto_root}/*.proto")
        list(APPEND proto_files ${root_proto_files})
    endforeach()

    set(generated_srcs)
    set(generated_hdrs)

    foreach(proto_file IN LISTS proto_files)
        set(relative_proto)
        foreach(proto_root IN LISTS proto_roots)
            file(RELATIVE_PATH candidate "${proto_root}" "${proto_file}")
            if(NOT candidate MATCHES "^\\.\\.")
                set(relative_proto "${candidate}")
                break()
            endif()
        endforeach()

        if(NOT relative_proto)
            message(FATAL_ERROR "Failed to resolve proto root for ${proto_file}")
        endif()

        get_filename_component(relative_dir "${relative_proto}" DIRECTORY)
        get_filename_component(proto_name_we "${relative_proto}" NAME_WE)

        set(pb_cc "${output_dir}/${relative_dir}/${proto_name_we}.pb.cc")
        set(pb_h "${output_dir}/${relative_dir}/${proto_name_we}.pb.h")
        set(grpc_cc "${output_dir}/${relative_dir}/${proto_name_we}.grpc.pb.cc")
        set(grpc_h "${output_dir}/${relative_dir}/${proto_name_we}.grpc.pb.h")

        add_custom_command(
            OUTPUT
                "${pb_cc}"
                "${pb_h}"
                "${grpc_cc}"
                "${grpc_h}"
            COMMAND
                $<TARGET_FILE:protobuf::protoc>
            ARGS
                ${proto_path_args}
                --cpp_out=${output_dir}
                --grpc_out=${output_dir}
                --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
                "${proto_file}"
            DEPENDS
                "${proto_file}"
                protobuf::protoc
                gRPC::grpc_cpp_plugin
            COMMENT "Generating gRPC sources for ${relative_proto}"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )

        list(APPEND generated_srcs "${pb_cc}" "${grpc_cc}")
        list(APPEND generated_hdrs "${pb_h}" "${grpc_h}")
    endforeach()

    set(${out_srcs} ${generated_srcs} PARENT_SCOPE)
    set(${out_hdrs} ${generated_hdrs} PARENT_SCOPE)
endfunction()
