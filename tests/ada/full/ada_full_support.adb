with Ada.Characters.Handling;

package body Ada_Full_Support is
   overriding function Area (Self : Triangle_Record) return Long_Float is (Self.B * Self.H / 2.0);
   overriding function Name (Self : Triangle_Record) return String is ("triangle");

   overriding function Area (Self : Failing_Record) return Long_Float is
   begin
      raise Sentinel with "from Ada";
      return 0.0;
   end Area;

   overriding function Name (Self : Loud_Record) return String is
     (Ada.Characters.Handling.To_Upper (Circle_Record (Self).Name));
   overriding function Name (Self : Noisy_Record) return String is
     (Ada.Characters.Handling.To_Lower (Circle_Record (Self).Name) & "!");

   overriding function Area (Self : Unit_Shape_Record) return Long_Float is (1.0);

   overriding function Visit (Self : in out Summer_Record; Value : Long_Float) return Long_Float is (Value * 10.0);
   overriding procedure Finished (Self : in out Summer_Record; Arg_1 : Long_Float_Vector) is
   begin
      Self.Seen := Arg_1;
   end Finished;

   function Twice (A : Long_Float) return Long_Float is (A * 2.0);
   function Plus_One (A : Long_Float) return Long_Float is (A + 1.0);

   function Failing (A : Long_Float) return Long_Float is
   begin
      raise Sentinel with "from Ada";
      return A;
   end Failing;

   function Via_Native (A : Long_Float) return Long_Float is
      F : Integer_To_Integer_Function := Adder (1);
      Result : constant Integer := F.Call (Integer (A));
   begin
      Free (F);
      return Long_Float (Result);
   end Via_Native;

   function Counting (A : Long_Float) return Long_Float is
   begin
      Calls := Calls + 1;
      return A;
   end Counting;
end Ada_Full_Support;
