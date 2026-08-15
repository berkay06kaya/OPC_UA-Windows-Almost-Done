cmake_minimum_required(VERSION 3.15)
project(OPC C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OPC_STATIC_BUILD "Statik MinGW runtime + statik Qt/OpenSSL/libmodbus ile tek dosya exe uret" OFF)
if(OPC_STATIC_BUILD AND MINGW)
    set(BUILD_SHARED_LIBS OFF)
    # MSYS2 zlib/png/freetype/pcre2 vb. paketleri hem .a (gercek statik) hem .dll.a (paylasimli
    # import lib) dosyalarini birlikte dagitir; CMake'in varsayilan find_library sirasi .dll.a'yi
    # once bulur. Bunu zorlamazsak Qt6Gui/Qt6Core statik derlense bile calisma zamaninda
    # zlib1.dll/libpng16.dll/freetype-6.dll'e bagimli kalir.
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

if(OPC_STATIC_BUILD AND MINGW)
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

if(OPC_STATIC_BUILD AND MINGW)
    # Statik libmodbus: MSYS2/paket yoneticileri libmodbus'u sadece paylasimli (.dll) olarak
    # dagitir, bu yuzden tools/static-deps altinda kaynaktan --enable-static ile ayrica derildi.
    set(MODBUS_STATIC_PREFIX "${CMAKE_SOURCE_DIR}/tools/static-deps/install" CACHE PATH
        "Kaynaktan statik derlenen libmodbus'un kurulum dizini")
    add_library(PkgConfig::MODBUS STATIC IMPORTED)
    set_target_properties(PkgConfig::MODBUS PROPERTIES
        IMPORTED_LOCATION "${MODBUS_STATIC_PREFIX}/lib/libmodbus.a"
        INTERFACE_INCLUDE_DIRECTORIES "${MODBUS_STATIC_PREFIX}/include/modbus")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MODBUS REQUIRED IMPORTED_TARGET libmodbus)
endif()

set(UA_ENABLE_ENCRYPTION OPENSSL CACHE STRING "" FORCE)
set(UA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(UA_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
# open62541 v1.3.11'in bazi goto/degisken-baslatma kaliplari, projenin varsayilan -Werror'u
# altinda cok yeni GCC surumlerinde (16.x) hataya donusuyor (-Wjump-misses-init).  Uyarilarin
# kendisi zararsiz -- sadece -Werror'u kapatiyoruz.
set(UA_FORCE_WERROR OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    open62541
    GIT_REPOSITORY https://github.com/open62541/open62541.git
    GIT_TAG        v1.3.11
)
FetchContent_MakeAvailable(open62541)

include_directories(src)

# Sertifika/anahtar .der dosyalarini C dizisi olarak gomen header uretimi (tools/embed_cert.cmake).
# Boylece uretilen tek-dosya exe, calisma zamaninda certs/ klasorune (mutlak yola) bagimli olmaz.
set(OPC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(OPC_CERT_FILE "${CMAKE_SOURCE_DIR}/certs/client_cert.der")
set(OPC_KEY_FILE "${CMAKE_SOURCE_DIR}/certs/client_key.der")
if(EXISTS "${OPC_CERT_FILE}" AND EXISTS "${OPC_KEY_FILE}")
    include(tools/embed_cert.cmake)
    opc_embed_der_as_header(
        "${OPC_CERT_FILE}" "${OPC_KEY_FILE}"
        "${OPC_GENERATED_DIR}/EmbeddedCerts.h")
    set(OPC_HAVE_EMBEDDED_CERTS TRUE)
else()
    set(OPC_HAVE_EMBEDDED_CERTS FALSE)
    message(WARNING "certs/client_cert.der veya certs/client_key.der bulunamadi -- "
        "sertifikalar exe'ye gomulmeyecek, calisma zamaninda OPC_CERT_DIR yolundan okunacak. "
        "Once 'cd certs && bash generate_certs.sh' calistirin.")
endif()

add_library(opccore STATIC
    src/core/DataStore.cpp
    src/core/GatewayManager.cpp
    src/core/Logger.cpp
    src/source/EndpointResolver.cpp
    src/source/OpcUaDataSource.cpp
    src/source/OpcUaBrowser.cpp
    src/source/OpcUaSubscriber.cpp
    src/source/UaUtils.cpp
    src/sink/ModbusConverter.cpp
    src/sink/ModbusFormatNames.cpp
    src/sink/ModbusSink.cpp
)
target_link_libraries(opccore PUBLIC
    open62541 OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_link_libraries(opccore PRIVATE PkgConfig::MODBUS)
target_compile_definitions(opccore PRIVATE OPC_CERT_DIR="${CMAKE_SOURCE_DIR}/certs")
if(OPC_HAVE_EMBEDDED_CERTS)
    target_compile_definitions(opccore PRIVATE OPC_HAVE_EMBEDDED_CERTS)
    target_include_directories(opccore PRIVATE "${OPC_GENERATED_DIR}")
endif()
if(WIN32)
    target_link_libraries(opccore PUBLIC ws2_32)
endif()

if(OPC_STATIC_BUILD AND MINGW)
    # MSYS2'nin md4c paketi hic statik CMake hedefi sunmuyor (sadece md4c::md4c -> .dll),
    # zstd paketi hem statik hem paylasimli sunuyor ama Qt'nin FindWrapZSTD.cmake modulu
    # paylasimliyi (zstd::libzstd_shared) once buluyor ve onu tercih ediyor. Qt'nin kendi
    # find modulleri "if(NOT TARGET WrapXxx::WrapXxx) ... endif()" ile korundugu icin, bu
    # hedefleri find_package(Qt6...) cagrisindan ONCE kendimiz gercek statik .a'lara isaret
    # edecek sekilde tanimlayip Qt'nin paylasimli secimini devre disi birakiyoruz.
    add_library(WrapSystemMd4c::WrapSystemMd4c STATIC IMPORTED)
    set_target_properties(WrapSystemMd4c::WrapSystemMd4c PROPERTIES
        IMPORTED_LOCATION "C:/msys64/ucrt64/lib/libmd4c.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/msys64/ucrt64/include")

    add_library(OPC_zstd_static STATIC IMPORTED)
    set_target_properties(OPC_zstd_static PROPERTIES
        IMPORTED_LOCATION "C:/msys64/ucrt64/lib/libzstd.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/msys64/ucrt64/include")
    add_library(WrapZSTD::WrapZSTD INTERFACE IMPORTED)
    set_target_properties(WrapZSTD::WrapZSTD PROPERTIES
        INTERFACE_LINK_LIBRARIES "OPC_zstd_static")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Quick)
qt_standard_project_setup()

qt_add_executable(OPC
    src/main.cpp
    src/qt/GuiApp.h
    src/qt/GuiApp.cpp
    src/qt/GuiSelfTest.h
    src/qt/GuiSelfTest.cpp
    src/qt/UnixSignalHandler.h
    src/qt/UnixSignalHandler.cpp
    src/qt/OpcUaController.h
    src/qt/OpcUaController.cpp
    src/qt/ModbusController.h
    src/qt/ModbusController.cpp
    src/qt/WatchRow.h
    src/qt/WatchedValuesModel.h
    src/qt/WatchedValuesModel.cpp
)
qt_add_resources(OPC "appqml" PREFIX "/" FILES src/qt/qml/Main.qml)
target_link_libraries(OPC PRIVATE opccore Qt6::Core Qt6::Quick)
set_target_properties(OPC PROPERTIES WIN32_EXECUTABLE TRUE)
if(OPC_STATIC_BUILD AND MINGW)
    # Statik Qt derlemesinde QML plugin'leri (QtQuick.Controls.Basic, QtQuick.Layouts, ...)
    # paylasimli degil, bu yuzden qmlimportscanner ile taranip exe'ye statik baglaniyor.
    qt_import_qml_plugins(OPC)
    # libbrotlidec.a, libbrotlicommon.a'daki sembollere bagimli. MSYS2'nin libbrotlidec.pc
    # dosyasi bunu "Requires.private" olarak dogru bildiriyor, ama Qt'nin FindWrapBrotli.cmake
    # modulu pkg_check_modules'u --static olmadan cagirdigi icin bu bagimliligi kaybediyor.
    # $<LINK_GROUP:RESCAN,...>, iki kutuphaneyi (script'teki cagri sirasindan bagimsiz olarak)
    # birbirine bitisik --start-group/--end-group ile sarip linker'i semboller cozulene kadar
    # tekrar tekrar taramaya zorluyor. RESCAN, INTERFACE (imported) hedeflerde calismadigi
    # icin (CMake uyariyor ve yok sayiyor) hedef adi yerine ham .a dosya yollari kullaniliyor.
    find_library(OPC_BROTLIDEC_LIB NAMES brotlidec libbrotlidec.a)
    find_library(OPC_BROTLICOMMON_LIB NAMES brotlicommon libbrotlicommon.a)
    target_link_libraries(OPC PRIVATE
        "$<LINK_GROUP:RESCAN,${OPC_BROTLIDEC_LIB},${OPC_BROTLICOMMON_LIB}>")
endif()
