#Install nginx files
message(STATUS, "Configuring nginx-related files with stream folder: ${WWW_DIR}")
configure_file(
    nginx.conf.in
    nginx.conf
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/nginx.conf" DESTINATION .)
configure_file(
    launch_nginx_docker.sh.in
    launch_nginx_docker.sh
    @ONLY
)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/launch_nginx_docker.sh" DESTINATION .)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/index.html" DESTINATION html)
