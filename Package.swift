// swift-tools-version:5.9
// Diskhopper Swift package: C++ core (with C bridge) + SwiftUI app.

import PackageDescription

let package = Package(
    name: "Diskhopper",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .library(name: "DiskhopperCore", targets: ["DiskhopperCore"]),
        .executable(name: "DiskhopperApp", targets: ["DiskhopperApp"]),
    ],
    targets: [
        .target(
            name: "DiskhopperCore",
            path: "core",
            publicHeadersPath: "bridge/include",
            cxxSettings: [
                .unsafeFlags(["-std=c++17"]),
                .headerSearchPath("bridge/include"),
                .headerSearchPath("scanner/include"),
                .headerSearchPath("classifier/include"),
                .headerSearchPath("cleaner/include"),
                .headerSearchPath("platform/include"),
                .headerSearchPath("rules/include"),
                .headerSearchPath("safety/include"),
            ]
        ),
        .executableTarget(
            name: "DiskhopperApp",
            dependencies: ["DiskhopperCore"],
            path: "macos/Sources/DiskhopperApp"
        ),
    ]
)