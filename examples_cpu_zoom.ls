import plottl
import io
fn ev() -> vec(CpuSchedEvent) {
  vec(CpuSchedEvent) e = []
  e.push(plottl.cpu_event(0, 6000000, 100, "main", 0, "app"))
  e.push(plottl.cpu_event(6000000, 9000000, 100, "main", 1, "app"))
  e.push(plottl.cpu_event(14000000, 20000000, 100, "main", 0, "app"))
  e.push(plottl.cpu_event(28000000, 34000000, 100, "main", 2, "app"))
  e.push(plottl.cpu_event(2000000, 7000000, 101, "worker-a", 4, "app"))
  e.push(plottl.cpu_event(9000000, 16000000, 101, "worker-a", 2, "app"))
  e.push(plottl.cpu_event(22000000, 30000000, 101, "worker-a", 4, "app"))
  e.push(plottl.cpu_event(3000000, 6000000, 102, "worker-b", 5, "app"))
  e.push(plottl.cpu_event(10000000, 18000000, 102, "worker-b", 3, "app"))
  e.push(plottl.cpu_event(20000000, 27000000, 102, "worker-b", 5, "app"))
  e.push(plottl.cpu_event(33000000, 40000000, 102, "worker-b", 1, "app"))
  e.push(plottl.cpu_event(11000000, 19000000, 103, "io-poll", 6, "app"))
  e.push(plottl.cpu_event(25000000, 38000000, 103, "io-poll", 6, "app"))
  return e
}
fn main() {
  plottl.CpuTopology topo = plottl.topology(8, 4)
  string html = plottl.cpu_timeline_html_zoom(ev(), topo, 1200, 240, "viridis")
  match io.write_file("cpu_timeline_zoom.html", html) { Ok(nb) => { print("wrote cpu_timeline_zoom.html") } Err(er) => { print("err "+er) } }
}
