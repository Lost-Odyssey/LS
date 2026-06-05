# alloc.rb — Ruby reference for the LS alloc benchmark.
#   ruby alloc.rb [n] [iters]

def vec_stress(n)
  v = []
  i = 0
  while i < n
    v.push("item_" + i.to_s)
    i += 1
  end
  chk = 0
  v.each { |s| chk += s.length }
  chk
end

def map_stress(n)
  freq = {}
  keyspace = 8192
  i = 0
  while i < n
    key = "key_" + (i % keyspace).to_s
    cur = freq.fetch(key, 0)
    freq[key] = cur + 1
    i += 1
  end
  freq.length
end

n = ARGV.length >= 1 ? ARGV[0].to_i : 200000
iters = ARGV.length >= 2 ? ARGV[1].to_i : 5

_warm = vec_stress(n) + map_stress(n)

total_ns = 0
chk = 0
iters.times do
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  chk += vec_stress(n) + map_stress(n)
  t1 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  total_ns += (t1 - t0)
end
mean_ns = total_ns.to_f / iters
puts "result: #{chk}"
puts format("[@bench] mean %.1f ns (%d iterations)", mean_ns, iters)
