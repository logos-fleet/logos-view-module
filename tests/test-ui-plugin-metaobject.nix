# The teardown surface of a GENERATED view plugin, checked BEHAVIOURALLY.
#
# `--backend ui` had no test at all for most of its life, which is how it came
# to be the one generated plugin class that did not publish the teardown hook.
# The reason that was invisible: ui-host reaches aboutToUnload() BY NAME through
# the meta-object, so a plugin class that never declares it simply has no such
# meta-method -- QMetaObject::invokeMethod returns false and the host moves on.
# That is indistinguishable from a view answering "Synchronous, nothing to wait
# for". No build fails, no load fails, no call fails; every view just silently
# and permanently loses its chance to finish.
#
# The same reasoning covers the second thing this check pins: the DECLARATION
# ORDER of the generated plugin's two members. `m_backend` declared before
# `m_logosModules` destroys the typed-deps aggregate first, so a backend
# destructor that calls modules() reads freed memory -- and says nothing,
# because isContextReady() answers off a pointer nobody clears. Text in a
# header is not evidence of a destruction order either, so the fixture backend
# records what it actually saw as it was destroyed.
#
# So this does not grep the generator, and it does not grep the generator's
# OUTPUT either -- text in a .cpp is not evidence that moc registered anything.
# It runs the generator, COMPILES the plugin it emitted into a real Qt plugin,
# loads that plugin with QPluginLoader, and drives the teardown handshake
# through the meta-object exactly as the host does.
{ pkgs, viewGenerator }:

pkgs.stdenv.mkDerivation {
  pname = "logos-view-ui-plugin-metaobject-test";
  version = "0.0.1";

  src = ./ui-plugin-metaobject;

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.qt6.wrapQtAppsNoGuiHook
    viewGenerator
  ];
  buildInputs = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtremoteobjects
  ];

  dontUseCmakeConfigure = true;

  # Both directories are this repo's own, and there is exactly one copy of
  # each: the templates a real ui_qml module instantiates, and the context
  # header its backend derives. Handing the fixture the real ones is what makes
  # this check able to fail when the emitter drifts from either.
  LOGOS_VIEW_TEMPLATE_DIR = "${../cmake}";
  LOGOS_VIEW_INCLUDE_DIR = "${../cpp}";

  buildPhase = ''
    runHook preBuild

    # ── Generate, with the migrated generator ────────────────────────────
    mkdir -p generated
    logos-view-generator --backend ui \
      --metadata metadata.json \
      --rep Ticker.rep \
      --output-dir generated

    for f in ticker_panel_ui_interface.h ticker_panel_ui_glue.h ticker_panel_ui_glue.cpp; do
      test -f "generated/$f" || { echo "FAIL: generator did not emit $f"; exit 1; }
    done

    # Q_PLUGIN_METADATA(... FILE "metadata.json") is resolved by moc relative
    # to the header it appears in, so the metadata must sit beside the emitted
    # glue header rather than at the fixture root.
    cp metadata.json generated/

    echo "--- generated glue header ---"
    cat generated/ticker_panel_ui_glue.h
    echo "--- end generated glue header ---"

    # ── Build the plugin the generator just described ────────────────────
    mkdir -p build && cd build
    cmake .. -GNinja \
      -DLOGOS_GENERATED_DIR="$PWD/../generated" \
      -DLOGOS_VIEW_TEMPLATE_DIR="$LOGOS_VIEW_TEMPLATE_DIR" \
      -DLOGOS_VIEW_INCLUDE_DIR="$LOGOS_VIEW_INCLUDE_DIR"
    ninja
    cd ..

    runHook postBuild
  '';

  doCheck = true;
  checkPhase = ''
    runHook preCheck

    PLUGIN="build/ticker_panel_plugin.so"
    if [ ! -f "$PLUGIN" ]; then
      PLUGIN="build/ticker_panel_plugin.dylib"
    fi
    if [ ! -f "$PLUGIN" ]; then
      echo "FAIL: generated view plugin was not built"
      ls -la build/
      exit 1
    fi

    # Loads the plugin and resolves aboutToUnload/unloadFinished by STRING,
    # then completes the handshake -- and finally destroys the plugin and reads
    # back the probe the backend's destructor wrote, which is the only way to
    # see the member DECLARATION order the emitter chose. See
    # ui-plugin-metaobject/main.cpp.
    ./build/ui_plugin_metaobject_check "$PWD/$PLUGIN" "$PWD/dtor_probe.txt"

    runHook postCheck
  '';

  installPhase = ''
    mkdir -p $out
    echo "generated view plugin publishes aboutToUnload/unloadFinished" > $out/result.txt
    echo "and its LogosModules aggregate outlives the backend" >> $out/result.txt
  '';
}
