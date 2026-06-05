# Ruby benchmark: character iteration over a long string
# Usage: ruby string.rb [n] [iters], defaults n=200000 iters=10

def count_char(s, c, ending)
  count = 0
  (0...ending).each do |i|
    count += 1 if s[i] == c
  end
  count
end

n = 200000
if ARGV.length > 0
  n = ARGV[0].to_i
  n = 200000 if n <= 0
end
iters = 10
if ARGV.length > 1
  iters = ARGV[1].to_i
  iters = 1 if iters < 1
end

pattern = "a b c d e f g "
plen = pattern.length  # 13
repeats = n / plen + 1
s = pattern * repeats

# warm-up
count_char(s, ' ', s.length)

result = 0
total_ns = 0

iters.times do
  ending = s.length
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  result += count_char(s, ' ', ending)
  t1 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
  total_ns += t1 - t0
end

mean_ns = total_ns.to_f / iters
puts "result: #{result}"
puts "[@bench] mean #{format('%.1f', mean_ns)} ns (#{iters} iterations)"
