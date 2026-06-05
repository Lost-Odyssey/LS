# Ruby benchmark: naive recursive Fibonacci
# Usage: ruby fib.rb [n] [iters], defaults n=35 iters=10

def fib(n)
  return n if n <= 1
  fib(n - 1) + fib(n - 2)
end

n = 35
if ARGV.length > 0
  n = ARGV[0].to_i
  n = 35 if n <= 0 || n > 45
end
iters = 10
if ARGV.length > 1
  iters = ARGV[1].to_i
  iters = 1 if iters < 1
end

# warm-up
fib(n)

result = 0
total_ns = 0

iters.times do
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  result += fib(n)
  t1 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  total_ns += t1 - t0
end

mean_ns = total_ns.to_f / iters
puts "result: #{result}"
puts "[@bench] mean #{format('%.1f', mean_ns)} ns (#{iters} iterations)"
