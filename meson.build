# SPDX-License-Identifier: MIT

project('nori', 'c',
  version : '0.1',
  license: 'MIT',
  default_options : [
    'warning_level=3',
    'c_std=c11',
  ],
)

add_project_arguments('-Wno-unused-parameter', language: 'c')
add_project_arguments('-Wno-overlength-strings', language: 'c')

cc = meson.get_compiler('c')

fontconfig = dependency('fontconfig')
freetype = dependency('freetype2')
harfbuzz = dependency('harfbuzz')

wl = dependency('wayland-client')
wl_cursor = dependency('wayland-cursor')
vulkan = dependency('vulkan')
# Only used for event-loop.
# Replace with a different implementation later.
wl_server = dependency('wayland-server')

math = cc.find_library('m')

scanner = dependency('wayland-scanner')
scanner = scanner.get_variable(pkgconfig: 'wayland_scanner')
scanner = find_program(scanner, native: true)

wl_protos = dependency('wayland-protocols')
wl_protos = wl_protos.get_variable(pkgconfig: 'pkgdatadir')

protos = [
  wl_protos / 'stable/xdg-shell/xdg-shell.xml',
  wl_protos / 'stable/presentation-time/presentation-time.xml',
  wl_protos / 'unstable/input-timestamps/input-timestamps-unstable-v1.xml',
]
proto_src = []

foreach proto : protos
  proto_src += custom_target(proto.underscorify() + '_c',
    input: proto,
    output: '@BASENAME@.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )

  proto_src += custom_target(proto.underscorify() + '_h',
    input: proto,
    output: '@BASENAME@-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

glslang = find_program('glslangValidator')

vert_h = custom_target('shader.vert.h',
  input: 'shader.vert',
  output: '@PLAINNAME@.h',
  command: [glslang, '-V', '--variable-name', 'vert_shader', '-o', '@OUTPUT@', '@INPUT@'])

frag_h = custom_target('shader.frag.h',
  input: 'shader.frag',
  output: '@PLAINNAME@.h',
  command: [glslang, '-V', '--variable-name', 'frag_shader', '-o', '@OUTPUT@', '@INPUT@'])

executable('nori',
  [
    'main.c',
    'scene.c',
    'scene-ops.c',
    'wayland.c',
    'wayland-surface.c',
    'vulkan.c',
    'vulkan-surface.c',
    'vulkan-renderpass.c',
    proto_src,
    vert_h,
    frag_h,
  ],
  dependencies: [
    fontconfig,
    freetype,
    harfbuzz,
    wl,
    wl_cursor,
    wl_server,
    vulkan,
    math,
  ],
  install : true,
)
