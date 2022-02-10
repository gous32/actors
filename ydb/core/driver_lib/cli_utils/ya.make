LIBRARY(cli_utils)

OWNER(g:kikimr)

SRCS(
    cli.cpp
    cli.h
    cli_actorsystem_perftest.cpp
    cli_cmd_config.h
    cli_cmd_config.cpp
    cli_cmds.h
    cli_cmds_admin.cpp
    cli_cmds_bs.cpp
    cli_cmds_cms.cpp
    cli_cmds_config.cpp
    cli_cmds_console.cpp
    cli_cmds_debug.cpp
    cli_cmds_disk.cpp
    cli_cmds_genconfig.cpp
    cli_cmds_get.cpp
    cli_cmds_group.cpp
    cli_cmds_node.cpp
    cli_cmds_root.cpp
    cli_cmds_server.cpp
    cli_cmds_tablet.cpp
    cli_cmds_tenant.cpp
    cli_fakeinitshard.cpp
    cli_keyvalue.cpp
    cli_persqueue.cpp 
    cli_persqueue_cluster_discovery.cpp
    cli_persqueue_stress.cpp 
    cli_load.cpp
    cli_minikql_compile_and_exec.cpp
    cli_mb_trace.cpp
    cli_scheme_cache_append.cpp
    cli_scheme_initroot.cpp
)

PEERDIR(
    library/cpp/deprecated/enum_codegen
    library/cpp/grpc/client
    library/cpp/protobuf/json
    library/cpp/yson
    ydb/core/actorlib_impl
    ydb/core/base
    ydb/core/blobstorage/pdisk
    ydb/core/client/minikql_compile
    ydb/core/client/scheme_cache_lib
    ydb/core/driver_lib/cli_base
    ydb/core/engine
    ydb/core/erasure
    ydb/core/mind/bscontroller
    ydb/core/protos
    ydb/core/scheme
    ydb/library/aclib
    ydb/library/folder_service/proto
    ydb/library/yaml_config
    ydb/public/api/grpc
    ydb/public/api/grpc/draft
    ydb/public/lib/deprecated/client
    ydb/public/lib/ydb_cli/common
)

YQL_LAST_ABI_VERSION()

END()
