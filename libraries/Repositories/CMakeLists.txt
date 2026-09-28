set(TARGET_NAME Repositories)
set(RAW_TARGET_NAME vlms_repositories)

set(REPOSITORIES_HEADERS
    include/VLMS/Repositories/ArchiveTypes.h
    include/VLMS/Repositories/CatalogRepository.h
    include/VLMS/Repositories/CatalogTypes.h
    include/VLMS/Repositories/CirculationRepository.h
    include/VLMS/Repositories/LoanPolicy.h
    include/VLMS/Repositories/LoanTypes.h
    include/VLMS/Repositories/MemberRepository.h
    include/VLMS/Repositories/MemberTypes.h
    include/VLMS/Repositories/MetricsRepository.h
    include/VLMS/Repositories/MetricsTypes.h
)

add_library(${RAW_TARGET_NAME}
    ${REPOSITORIES_HEADERS}
    src/RepoSql.h
    src/BookSql.h
    src/BookSql.cpp
    src/MemberSql.h
    src/MemberSql.cpp
    src/LoanSql.h
    src/LoanSql.cpp
    src/NamedEntityStore.h
    src/NamedEntityStore.cpp
    src/BookCopyStore.h
    src/BookCopyStore.cpp
    src/CategoryStore.h
    src/CategoryStore.cpp
    src/LoanPolicy.cpp
    src/CatalogRepository.cpp
    src/MemberRepository.cpp
    src/CirculationRepository.cpp
    src/MetricsRepository.cpp
)
add_library(${CMAKE_ROOT_NAME}::${TARGET_NAME} ALIAS ${RAW_TARGET_NAME})

target_include_directories(${RAW_TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${RAW_TARGET_NAME}
    PUBLIC
        ${CMAKE_ROOT_NAME}::Database
)

vlms_configure_target(${RAW_TARGET_NAME})

set_target_properties(${RAW_TARGET_NAME} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

install(TARGETS ${RAW_TARGET_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
