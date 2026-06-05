# json_bench.rb — Ruby reference for JSON benchmark.
#   ruby json_bench.rb [n] [iters]
require 'json'

def build_json(n)
  records = (0...n).map do |i|
    { id: i, name: "user_#{i}", score: i * 7, active: i.even? }
  end
  JSON.generate(records)
end

n = ARGV.length >= 1 ? ARGV[0].to_i : 1000
iters = ARGV.length >= 2 ? ARGV[1].to_i : 5

raw = build_json(n)

# warm-up
_w = JSON.generate(JSON.parse(raw))

parse_ns = 0
stringify_ns = 0
chk = 0
iters.times do
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  obj = JSON.parse(raw)
  t1 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  parse_ns += (t1 - t0)

  t2 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  out = JSON.generate(obj)
  t3 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  stringify_ns += (t3 - t2)
  chk += out.length
end

parse_mean = parse_ns.to_f / iters
str_mean = stringify_ns.to_f / iters
total_mean = parse_mean + str_mean
puts "result: #{chk}"
puts format("[@bench] mean %.1f ns (%d iterations)", total_mean, iters)
puts format("  parse:     %.1f ns", parse_mean)
puts format("  stringify: %.1f ns", str_mean)
