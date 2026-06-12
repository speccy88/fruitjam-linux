var fruitjam_lib = module("fruitjam_lib")

fruitjam_lib.clamp = def(value, low, high)
    if value < low
        return low
    end
    if value > high
        return high
    end
    return value
end

fruitjam_lib.join_label = def(prefix, index)
    return prefix + str(index)
end

class Counter
    var value
    def init(start)
        self.value = start
    end
    def step(delta)
        self.value += delta
        return self.value
    end
end

fruitjam_lib.Counter = Counter

return fruitjam_lib
