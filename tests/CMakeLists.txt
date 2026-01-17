set(TEST_SOURCES
    test_main.cpp
    unit_amf0.cpp
    unit_chunks.cpp
    unit_socket.cpp
    unit_handshake.cpp
    unit_encoder_openh264.cpp
    integration_relay.cpp
)

set(LIB_SOURCES
    ../src/core/log.cpp
    ../src/core/cli.cpp
    ../src/net/buffer.cpp
    ../src/net/socket.cpp
    ../src/net/epoll_loop.cpp
    ../src/rtmp/amf0.cpp
    ../src/rtmp/rtmp_chunks.cpp
    ../src/rtmp/rtmp_messages.cpp
    ../src/rtmp/rtmp_handshake.cpp
    ../src/rtmp/rtmp_session.cpp
    ../src/rtmp/rtmp_server.cpp
    ../src/rtmp/rtmp_client.cpp
    ../src/relay/stream_id.cpp
    ../src/relay/policy.cpp
    ../src/relay/relay_manager.cpp
    ../src/transcode/decoder_openh264.cpp
    ../src/transcode/decoder_fdk_aac.cpp
    ../src/transcode/encoder_openh264.cpp
    ../src/transcode/encoder_fdk_aac.cpp
    ../src/transcode/scaler_libyuv.cpp
    ../src/transcode/transcoder.cpp
)

add_executable(rtmp_abr_tests
    ${TEST_SOURCES}
    ${LIB_SOURCES}
)

target_link_libraries(rtmp_abr_tests 
    pthread
    ${OPENH264_LIBRARIES}
    ${FDK_AAC_LIBRARIES}
    ${LIBYUV_LIBRARIES}
)

target_include_directories(rtmp_abr_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${OPENH264_INCLUDE_DIRS}
    ${FDK_AAC_INCLUDE_DIRS}
    ${LIBYUV_INCLUDE_DIRS}
)
