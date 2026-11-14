% A subclass object C++ takes ownership of; Octave state (tag) survives.
classdef UnitShape < features.Shape
  properties
    tag = []
  end
  methods
    function r = area (obj)
      r = 1;
    end
  end
end
