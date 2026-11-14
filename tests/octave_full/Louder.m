% A deeper subclass inherits Noisy's override.
classdef Louder < Noisy
  methods
    function obj = Louder (r)
      obj@Noisy (r);
    end
  end
end
