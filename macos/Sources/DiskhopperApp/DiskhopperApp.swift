import SwiftUI

@main
struct DiskhopperApp: App {
    @StateObject private var model = DiskhopperModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(model)
                .frame(minWidth: 780, minHeight: 540)
        }
    }
}