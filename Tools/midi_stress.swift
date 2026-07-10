import CoreMIDI
import Foundation

private let destinationName = "USB MIDI DevicePort 1"

private func endpointName(_ endpoint: MIDIEndpointRef) -> String {
    var value: Unmanaged<CFString>?
    guard MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &value) == noErr,
          let name = value?.takeRetainedValue() else { return "" }
    return name as String
}

private func destination(named name: String) -> MIDIEndpointRef {
    for index in 0..<MIDIGetNumberOfDestinations() {
        let endpoint = MIDIGetDestination(index)
        if endpointName(endpoint) == name { return endpoint }
    }
    return 0
}

private func usage() -> Never {
    FileHandle.standardError.write(Data("usage: midi_stress.swift batch <channel> <pairs> <interval-us> | marker <channel> <pitch>\n".utf8))
    exit(2)
}

let arguments = Array(CommandLine.arguments.dropFirst())
if arguments == ["--help"] || arguments == ["-h"] { usage() }
guard arguments.count == 4 || arguments.count == 3 else { usage() }

let mode = arguments[0]
guard let channel = Int(arguments[1]), (1...16).contains(channel) else { usage() }
let endpoint = destination(named: destinationName)
guard endpoint != 0 else { fatalError("CoreMIDI destination not found: \(destinationName)") }

var client = MIDIClientRef()
var output = MIDIPortRef()
precondition(MIDIClientCreate("Wingie2 MIDI stress" as CFString, nil, nil, &client) == noErr)
precondition(MIDIOutputPortCreate(client, "Wingie2 MIDI stress out" as CFString, &output) == noErr)
defer {
    MIDIPortDispose(output)
    MIDIClientDispose(client)
}

func send(_ bytes: [UInt8]) {
    var list = MIDIPacketList()
    withUnsafeMutablePointer(to: &list) { listPointer in
        let packet = MIDIPacketListInit(listPointer)
        bytes.withUnsafeBufferPointer { buffer in
            _ = MIDIPacketListAdd(listPointer, MemoryLayout<MIDIPacketList>.size, packet, 0,
                                  buffer.count, buffer.baseAddress!)
        }
        precondition(MIDISend(output, endpoint, listPointer) == noErr)
    }
}

let noteOn = UInt8(0x90 | (channel - 1))
let noteOff = UInt8(0x80 | (channel - 1))
let started = ContinuousClock.now

switch mode {
case "batch":
    guard arguments.count == 4,
          let pairs = Int(arguments[2]), pairs > 0,
          let interval = useconds_t(arguments[3]) else { usage() }
    for index in 0..<pairs {
        let pitch = UInt8(36 + (index % 60))
        send([noteOn, pitch, 100])
        usleep(interval)
        send([noteOff, pitch, 0])
        usleep(interval)
    }
    print("sent channel=\(channel) note_on=\(pairs) note_off=\(pairs) elapsed=\(started.duration(to: .now))")
case "marker":
    guard arguments.count == 3, let pitchValue = Int(arguments[2]),
          (0...127).contains(pitchValue) else { usage() }
    let pitch = UInt8(pitchValue)
    send([noteOn, pitch, 100])
    usleep(100_000)
    send([noteOff, pitch, 0])
    print("marker channel=\(channel) pitch=\(pitch)")
default:
    usage()
}
