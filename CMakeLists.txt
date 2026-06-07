cmake_minimum_required(VERSION 3.16)
project(bedrock-protocol-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BEDROCK_PROTOCOL_CPP_BUILD_TOOLS "Build command-line helpers: bedrock, bedrock-session, login, ping" ON)
option(BEDROCK_PROTOCOL_CPP_BUILD_BOT "Build local bot that links bedrock_protocol" ON)
option(BEDROCK_PROTOCOL_CPP_BUILD_EXAMPLES "Build example bots" ON)
option(BEDROCK_PROTOCOL_CPP_INSTALL "Enable install rules" ON)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(bedrock_protocol
    src/BinaryStream.cpp
    src/RakNetPing.cpp
    src/RakNetConnect.cpp
    src/BedrockFramer.cpp
    src/LoginPacket.cpp
    src/BedrockEncryption.cpp
    src/BedrockKeyExchange.cpp
    src/auth/BedrockAuthJwt.cpp
    src/auth/BedrockClientDataBuilder.cpp
    src/events/BedrockPacketEventAdapter.cpp
    src/generated/GeneratedProtocolTables.cpp
    src/protocol/BatchCodec.cpp
    src/protocol/BedrockClientRuntime.cpp
    src/protocol/BedrockPacketIO.cpp
    src/protocol/BedrockProtocolClient.cpp
    src/protocol/GamePacketCodec.cpp
    src/protocol/PacketDispatcher.cpp
    src/protocol/PacketFactory.cpp
    src/protocol/PacketPayloadReader.cpp
    src/protocol/PacketRegistry.cpp
    src/protocol/TypedPacketDispatcher.cpp
    src/protocol/VersionedMcpeCodec.cpp
    src/server/RakNetServer.cpp
    src/util/BinaryStream.cpp
    src/util/VarInt.cpp
)
add_library(BedrockProtocol::bedrock_protocol ALIAS bedrock_protocol)
add_library(Bedrock::bedrock ALIAS bedrock_protocol)

# Backward compatible target name for old local tests.
add_library(bedrock_core ALIAS bedrock_protocol)

set_target_properties(bedrock_protocol PROPERTIES
    OUTPUT_NAME bedrock_protocol
    EXPORT_NAME bedrock_protocol
)

target_include_directories(bedrock_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(bedrock_protocol PUBLIC ZLIB::ZLIB OpenSSL::Crypto)
target_compile_definitions(bedrock_protocol PUBLIC _DEFAULT_SOURCE)

if(BEDROCK_PROTOCOL_CPP_BUILD_TOOLS)
    add_executable(bedrock tools/bedrock.cpp)
    target_link_libraries(bedrock PRIVATE BedrockProtocol::bedrock_protocol)

    add_executable(login tools/login.cpp)
    target_link_libraries(login PRIVATE BedrockProtocol::bedrock_protocol)

    add_executable(xbox-login tools/xbox-login.cpp)
    target_link_libraries(xbox-login PRIVATE BedrockProtocol::bedrock_protocol)

    add_executable(ping tools/bedrock_ping.cpp)
    target_link_libraries(ping PRIVATE BedrockProtocol::bedrock_protocol)

    add_executable(protocol-roundtrip tools/protocol_roundtrip.cpp)
    target_link_libraries(protocol-roundtrip PRIVATE BedrockProtocol::bedrock_protocol)

    add_executable(raknet-server-smoke tools/raknet_server_smoke.cpp)
    target_link_libraries(raknet-server-smoke PRIVATE BedrockProtocol::bedrock_protocol)

    # Runtime helper used by the public API and the bedrock launcher.
    add_executable(bedrock-session tools/session.cpp)
    target_link_libraries(bedrock-session PRIVATE BedrockProtocol::bedrock_protocol)
endif()

if(BEDROCK_PROTOCOL_CPP_BUILD_BOT)
    add_executable(bot bots/bot.cpp)
    target_link_libraries(bot PRIVATE BedrockProtocol::bedrock_protocol)
endif()

if(BEDROCK_PROTOCOL_CPP_BUILD_EXAMPLES)
    add_executable(simple-create-client-bot examples/simple_create_client_bot.cpp)
    target_link_libraries(simple-create-client-bot PRIVATE BedrockProtocol::bedrock_protocol)

    add_executable(packet-event-bot examples/packet_event_bot.cpp)
    target_link_libraries(packet-event-bot PRIVATE BedrockProtocol::bedrock_protocol)

    add_executable(medium-bot examples/medium_bot.cpp)
    target_link_libraries(medium-bot PRIVATE BedrockProtocol::bedrock_protocol)

    add_executable(relay-packet-bot examples/relay_packet_bot.cpp)
    target_link_libraries(relay-packet-bot PRIVATE BedrockProtocol::bedrock_protocol)

    add_executable(simple-server examples/simple_server.cpp)
    target_link_libraries(simple-server PRIVATE BedrockProtocol::bedrock_protocol)
endif()

if(BEDROCK_PROTOCOL_CPP_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS bedrock_protocol
        EXPORT BedrockProtocolTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    if(BEDROCK_PROTOCOL_CPP_BUILD_TOOLS)
        install(TARGETS bedrock bedrock-session login xbox-login ping protocol-roundtrip raknet-server-smoke
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(BEDROCK_PROTOCOL_CPP_BUILD_EXAMPLES)
        install(TARGETS simple-create-client-bot packet-event-bot medium-bot relay-packet-bot simple-server
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY docs DESTINATION ${CMAKE_INSTALL_DATADIR}/bedrock-protocol-cpp OPTIONAL)
    install(FILES data/client_data_template.json DESTINATION ${CMAKE_INSTALL_DATADIR}/bedrock-protocol-cpp/data)
    install(DIRECTORY data/minecraft-data DESTINATION ${CMAKE_INSTALL_DATADIR}/bedrock-protocol-cpp/data OPTIONAL)
    install(DIRECTORY data/generated DESTINATION ${CMAKE_INSTALL_DATADIR}/bedrock-protocol-cpp/data OPTIONAL)

    install(EXPORT BedrockProtocolTargets
        FILE BedrockProtocolTargets.cmake
        NAMESPACE BedrockProtocol::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BedrockProtocol
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BedrockProtocolConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/BedrockProtocolConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BedrockProtocol
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/BedrockProtocolConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/BedrockProtocolConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/BedrockProtocolConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BedrockProtocol
    )
endif()
