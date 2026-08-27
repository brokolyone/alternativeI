# Included by `cpack` once per generator listed in CPACK_GENERATOR, with
# CPACK_GENERATOR reset beforehand to the single generator currently being
# packaged (see CPACK_PROJECT_CONFIG_FILE in CMakeLists.txt and the CPack
# module docs). Used here only to give the portable archive generators
# (ZIP on Windows, TGZ on Linux) a "-portable" filename - they'd otherwise
# share the same CPACK_PACKAGE_FILE_NAME base as the installer/.deb and be
# distinguishable only by file extension.
if(CPACK_GENERATOR STREQUAL "ZIP" OR CPACK_GENERATOR STREQUAL "TGZ")
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}-portable")
endif()
