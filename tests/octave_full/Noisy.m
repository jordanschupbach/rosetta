classdef Noisy < features.Circle
  methods
    function obj = Noisy (r)
      obj@features.Circle (r);
    end
    function r = name (obj)
      r = [lower(name@features.Circle (obj)) '!'];
    end
  end
end
