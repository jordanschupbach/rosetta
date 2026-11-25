with Ada.Characters.Handling;

package body Ada_Names_Support is
   overriding function Label (Self : Shouting_Record) return String is
     (Ada.Characters.Handling.To_Upper (Named_Record (Self).Label));
   overriding function Close_2 (Self : in out Pipe_Record) return String is ("pipe " & Resource_Record (Self).Close_2);
   overriding function Describe (Self : Deep_Record) return String is ("deep " & Leaf_Record (Self).Describe);
   overriding function Label (Self : Loud_Record) return String is ("loud " & Thing_Record (Self).Label);

   function Inc1 (A : Integer) return Integer is (A + 1);

   function Failing (A : Integer) return Integer is
   begin
      raise Binding_Error with "from a subprogram";
      return A;
   end Failing;
end Ada_Names_Support;
