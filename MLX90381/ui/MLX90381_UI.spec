# -*- mode: python ; coding: utf-8 -*-

# Filter out unnecessary Qt binaries and translations
def filter_binaries(binaries):
    excludes = [
        'Qt6Quick', 'Qt6Qml', 'Qt6Network', 'Qt6Svg', 'Qt6Pdf',
        'Qt6WebEngine', 'Qt6Multimedia', 'Qt6OpenGL', 'Qt63D',
        'Qt6ShaderTools', 'Qt6VirtualKeyboard', 'Qt6Positioning',
        'opengl32sw',  # Software OpenGL (large!)
        'd3dcompiler',  # DirectX compiler
    ]
    return [(name, path, typ) for name, path, typ in binaries
            if not any(ex.lower() in name.lower() for ex in excludes)]

a = Analysis(
    ['main.py'],
    pathex=[],
    binaries=[],
    datas=[],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        # Exclude heavy unused PySide6 modules
        'PySide6.QtWebEngine',
        'PySide6.QtWebEngineCore',
        'PySide6.QtWebEngineWidgets',
        'PySide6.QtWebChannel',
        'PySide6.Qt3DCore',
        'PySide6.Qt3DRender',
        'PySide6.Qt3DInput',
        'PySide6.Qt3DLogic',
        'PySide6.Qt3DAnimation',
        'PySide6.Qt3DExtras',
        'PySide6.QtMultimedia',
        'PySide6.QtMultimediaWidgets',
        'PySide6.QtPositioning',
        'PySide6.QtLocation',
        'PySide6.QtBluetooth',
        'PySide6.QtNfc',
        'PySide6.QtSensors',
        'PySide6.QtQuick',
        'PySide6.QtQuickWidgets',
        'PySide6.QtQml',
        'PySide6.QtDesigner',
        'PySide6.QtHelp',
        'PySide6.QtSql',
        'PySide6.QtSvg',
        'PySide6.QtTest',
        'PySide6.QtXml',
        'PySide6.QtNetwork',
        'PySide6.QtOpenGL',
        'PySide6.QtOpenGLWidgets',
        'PySide6.QtPdf',
        'PySide6.QtPdfWidgets',
        'PySide6.QtCharts',
        'PySide6.QtDataVisualization',
    ],
    noarchive=False,
    optimize=2,
)

# Apply binary filter
a.binaries = filter_binaries(a.binaries)

# Also remove translations (saves ~5MB)
a.datas = [(name, path, typ) for name, path, typ in a.datas
           if not name.startswith('PySide6/translations')]

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='MLX90381_UI',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
