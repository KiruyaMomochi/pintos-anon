##
const PRI_MAX = 63;
const PRI_MIN = 0;
ready_threads = 3;

calc_priority(recent_cpu, nice) = PRI_MAX - (recent_cpu / 4) - (nice * 2);
calc_recent_cpu(load_avg, recent_cpu, nice) = (2*load_avg) / (2*load_avg + 1) * recent_cpu + nice;
calc_load_avg(load_avg, ready_threads) = (59/60) * load_avg + (1/60) * ready_threads;
current_thread = 1;

function tick()
    global time += 1;

	if priority[current_thread] != maximum(priority)
		global current_thread = argmax(priority)
	end
    recent_cpu[current_thread] += 1;
    
    if time % 100 == 0 # which do first?
        global load_avg = calc_load_avg.(load_avg, ready_threads);
        global recent_cpu = calc_recent_cpu.(load_avg, recent_cpu, nice);
    end;

    if time % 4 == 0
        global priority = calc_priority.(recent_cpu, nice);
        global priority = clamp.(priority, PRI_MIN, PRI_MAX);
    end;
end;
##
recent_cpu = zeros(3);
load_avg = zeros(3);
nice = [0.:2.;];
time = 0;
priority = calc_priority.(recent_cpu, nice)

for i in 1:36
	tick();
	if time % 4 == 0
       println("$time: $current_thread, $recent_cpu, $priority")
	end
end
##
