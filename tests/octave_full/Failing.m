% An override raising an Octave error, which C++ propagates.
classdef Failing < features.Shape
  methods
    function r = area (obj)
      error ('Failing:area', 'from Octave');
    end
  end
end
