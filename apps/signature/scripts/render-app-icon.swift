// Saves the icon macOS shows for an app, drawn on a blue background.
//   swift render-app-icon.swift /path/to/Some.app out.png
import AppKit

let args = CommandLine.arguments
let size = 512
let icon = NSWorkspace.shared.icon(forFile: args[1])
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
                           bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                           colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
let frame = NSRect(x: 0, y: 0, width: size, height: size)
NSColor(red: 0.15, green: 0.45, blue: 0.9, alpha: 1).setFill()
frame.fill()
icon.draw(in: frame)
NSGraphicsContext.restoreGraphicsState()
try rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: args[2]))
