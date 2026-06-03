import plottl
import io

fn make_events() -> vec(CpuSchedEvent) {
    vec(CpuSchedEvent) ev = []
    // tid, name, cpu_id ; topology: 4 physical cores, 8 logical (HT = cpu 4..7)
    ev.push(plottl.cpu_event(0,        4000000,  100, "main",     0, "app"))
    ev.push(plottl.cpu_event(4000000,  9000000,  100, "main",     1, "app"))
    ev.push(plottl.cpu_event(2000000,  7000000,  101, "worker-a", 4, "app"))   // HT of core 0
    ev.push(plottl.cpu_event(8000000,  13000000, 101, "worker-a", 2, "app"))
    ev.push(plottl.cpu_event(3000000,  6000000,  102, "worker-b", 5, "app"))   // HT of core 1
    ev.push(plottl.cpu_event(7000000,  14000000, 102, "worker-b", 3, "app"))
    ev.push(plottl.cpu_event(10000000, 16000000, 103, "io-poll",  6, "app"))   // HT of core 2
    return ev
}

fn main() {
    plottl.CpuTopology topo = plottl.topology(8, 4)

    // SVG -> file
    string svg = plottl.cpu_timeline_svg(make_events(), topo, plottl.CpuPlotOpts{w: 900, h: 360})
    match io.write_file("cpu_timeline_demo.svg", svg) {
        Ok(nbytes) => { print("wrote cpu_timeline_demo.svg") }
        Err(e) => { print("write error: " + e) }
    }

    // terminal text
    print("")
    print(plottl.cpu_timeline_text(make_events(), 60))
}
