-- Copyright (c) 2002-2009 Tampere University.
--
-- This file is part of TTA-Based Codesign Environment (TCE).
-- 
-- Permission is hereby granted, free of charge, to any person obtaining a
-- copy of this software and associated documentation files (the "Software"),
-- to deal in the Software without restriction, including without limitation
-- the rights to use, copy, modify, merge, publish, distribute, sublicense,
-- and/or sell copies of the Software, and to permit persons to whom the
-- Software is furnished to do so, subject to the following conditions:
-- 
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
-- 
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
-- FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
-- DEALINGS IN THE SOFTWARE.
-------------------------------------------------------------------------------
-- Title      : testbench for TTA processor
-- Project    : FlexDSP
-------------------------------------------------------------------------------
-- File       : testbench.vhdl
-- Author     : Jaakko Sertamo  <sertamo@vlad.cs.tut.fi>
-- Company    : TUT/IDCS
-- Created    : 2001-07-13
-- Last update: 2007/04/03
-- Platform   : 
-------------------------------------------------------------------------------
-- Description: Simply resets the processor and triggers execution
-------------------------------------------------------------------------------
-- Revisions  :
-- Date        Version  Author  Description
-- 2001-07-13  1.0      sertamo Created
-------------------------------------------------------------------------------

library IEEE;
use IEEE.Std_Logic_1164.all;
use IEEE.numeric_std.all;
use IEEE.math_real.all;
use work.almaif_core_imem_mau.all;
use work.almaif_core_globals.all;
use work.register_pkg.all;
use work.tce_util.all;
use std.textio.all;
use IEEE.std_logic_textio.all;
use work.almaif_core_toplevel_params.all;

entity tta_almaif_tb is
  generic (
    imem_image : string := "../prog.img";
    log_path   : string := "../run.log";
    clk_period : time := PERIOD);
end tta_almaif_tb;

architecture testbench of tta_almaif_tb is
  function mmax (a,b : integer) return integer is
  begin
    if (a > b) then return a; else return b; end if;
  end mmax;

  constant stall_threshold : real := 0.1;
  constant core_dbg_addrw : integer := 32;
  constant core_dbg_dataw : integer := 32;
  constant imem_word_width_c  : integer := (IMEMDATAWIDTH+31)/32;
  constant imem_word_sel_c    : integer := bit_width(imem_word_width_c-1);
  constant id_width_c         : integer := 3;
  signal clk       : std_logic := '0';
  signal clk_ena   : std_logic := '1';
  signal nreset    : std_logic;

  constant io_addrw_c : integer := 17-2;
  constant IMEM_OFFSET : integer := 2**(io_addrw_c);
  constant DMEM_OFFSET : integer := (2**(io_addrw_c))*2;
  constant PMEM_OFFSET : integer := (2**(io_addrw_c))*3;

  type data_arr is array (natural range <>) of std_logic_vector(31 downto 0);
  signal in_data   : data_arr(0 to 63);


  type imem_arr is array (natural range <>) of std_logic_vector(15 downto 0);

  signal   val_out : std_logic_vector(31 downto 0);
  signal   vals_in  : data_arr(0 to 63);
  signal   vals_out : data_arr(0 to 63);

  -- AXI-lite interface
  signal s_axi_awaddr   : STD_LOGIC_VECTOR (32-1 downto 0);
  signal s_axi_awvalid  : STD_LOGIC;
  signal s_axi_awready  : STD_LOGIC;
  signal s_axi_wdata    : STD_LOGIC_VECTOR (31 downto 0);
  signal s_axi_wstrb    : STD_LOGIC_VECTOR (3 downto 0);
  signal s_axi_wvalid   : STD_LOGIC;
  signal s_axi_wready   : STD_LOGIC;
  signal s_axi_bresp    : STD_LOGIC_VECTOR (2-1 downto 0);
  signal s_axi_bvalid   : STD_LOGIC;
  signal s_axi_bready   : STD_LOGIC;
  signal s_axi_araddr   : STD_LOGIC_VECTOR (32-1 downto 0);
  signal s_axi_arvalid  : STD_LOGIC;
  signal s_axi_arready  : STD_LOGIC;
  signal s_axi_rdata    : STD_LOGIC_VECTOR (31 downto 0);
  signal s_axi_rresp    : STD_LOGIC_VECTOR (2-1 downto 0);
  signal s_axi_rvalid   : STD_LOGIC;
  signal s_axi_rready   : STD_LOGIC;
  signal s_axi_awid     : STD_LOGIC_VECTOR (id_width_c-1 downto 0);
  signal s_axi_awlen    : STD_LOGIC_VECTOR (8-1 downto 0);
  signal s_axi_awsize   : STD_LOGIC_VECTOR (3-1 downto 0);
  signal s_axi_awburst  : STD_LOGIC_VECTOR (2-1 downto 0);
  signal s_axi_bid      : STD_LOGIC_VECTOR (id_width_c-1 downto 0);
  signal s_axi_arid     : STD_LOGIC_VECTOR (id_width_c-1 downto 0);
  signal s_axi_arlen    : STD_LOGIC_VECTOR (8-1 downto 0);
  signal s_axi_arsize   : STD_LOGIC_VECTOR (3-1 downto 0);
  signal s_axi_arburst  : STD_LOGIC_VECTOR (2-1 downto 0);
  signal s_axi_rid      : STD_LOGIC_VECTOR (id_width_c-1 downto 0);
  signal s_axi_rlast    : STD_LOGIC;

  signal s_axi_wlast   : std_logic;

  function endian_swap(a : std_logic_vector) return std_logic_vector is
    variable result : std_logic_vector(a'high downto 0);
    variable j   : integer;
  begin
    for i in 0 to (a'high+1)/8-1 loop
        j := (a'high+1)/8-1-i;
        result((i+1)*8-1 downto i*8) := a((j+1)*8-1 downto j*8);
    end loop;
    return result;
  end;

begin

    clk_gen : process(clk, clk_ena)
  begin
    if clk_ena = '1' then
      clk <= not clk after PERIOD/2;
    end if;
  end process clk_gen;

  dut : entity work.almaif_core_toplevel
    generic map (
      axi_id_width_g => id_width_c
    ) port map (
      clk           => clk,
      rstx          => nreset,
      s_axi_awaddr  => s_axi_awaddr(io_addrw_c+2-1 downto 0),
      s_axi_awvalid => s_axi_awvalid,
      s_axi_awready => s_axi_awready,
      s_axi_wdata   => s_axi_wdata,
      s_axi_wstrb   => s_axi_wstrb,
      s_axi_wvalid  => s_axi_wvalid,
      s_axi_wready  => s_axi_wready,
      s_axi_bresp   => s_axi_bresp,
      s_axi_bvalid  => s_axi_bvalid,
      s_axi_bready  => s_axi_bready,
      s_axi_araddr  => s_axi_araddr(io_addrw_c+2-1 downto 0),
      s_axi_arvalid => s_axi_arvalid,
      s_axi_arready => s_axi_arready,
      s_axi_rdata   => s_axi_rdata,
      s_axi_rresp   => s_axi_rresp,
      s_axi_rvalid  => s_axi_rvalid,
      s_axi_rready  => s_axi_rready,
      -- Full AXI4
      s_axi_awid     => s_axi_awid,
      s_axi_awlen    => s_axi_awlen,
      s_axi_awsize   => s_axi_awsize,
      s_axi_awburst  => s_axi_awburst,
      s_axi_bid      => s_axi_bid,
      s_axi_arid     => s_axi_arid,
      s_axi_arlen    => s_axi_arlen,
      s_axi_arsize   => s_axi_arsize,
      s_axi_arburst  => s_axi_arburst,
      s_axi_rid      => s_axi_rid,
      s_axi_rlast    => s_axi_rlast
      );

  run_test : process

    procedure axi_write (address : in integer;
                         value : in std_logic_vector(core_dbg_dataw - 1 downto 0)) is

      variable addr_vec : std_logic_vector(core_dbg_addrw - 1 downto 0)
        := std_logic_vector(to_unsigned(address, core_dbg_addrw));

    begin
      s_axi_awlen <= (others => '0');

      s_axi_awvalid <= '1';
      s_axi_awaddr <= std_logic_vector(resize(unsigned(addr_vec), 32));

      while s_axi_awready = '0' loop
        wait until rising_edge(clk);
      end loop;
      s_axi_awvalid <= '0';
      s_axi_wvalid <= '1';
      s_axi_wdata <= value;
      s_axi_wlast <= '1';
      wait until rising_edge(clk);
      while s_axi_wready = '0' loop
        wait until rising_edge(clk);
      end loop;
      s_axi_wvalid <= '0';
      wait until rising_edge(clk);

      s_axi_wlast <= '0';
      s_axi_wvalid <= '0';
      s_axi_awvalid <= '0';

    end procedure axi_write;

    procedure axi_bwrite (address : in integer;
                          length  : in integer;
                          value : in data_arr;
                          variable seed1 : inout integer;
                          variable seed2 : inout integer) is

      variable addr_vec : std_logic_vector(core_dbg_addrw - 1 downto 0)
        := std_logic_vector(to_unsigned(address, core_dbg_addrw));
      variable I : integer := 0;
      variable rand : real;

    begin
      s_axi_awlen <= std_logic_vector(to_unsigned(length-1, 8));

      s_axi_awvalid <= '1';
      s_axi_awaddr <= std_logic_vector(resize(unsigned(addr_vec), 32));
      while s_axi_awready = '0' loop
        wait until rising_edge(clk);
      end loop;
      s_axi_awvalid <= '0';

      while I < length loop
        uniform(seed1, seed2, rand);
        if rand < stall_threshold then
          s_axi_wvalid <= '1';
          s_axi_wdata <= value(I);

          if I = length-1 then
            s_axi_wlast <= '1';
          end if;

          wait until rising_edge(clk);
          while s_axi_wready = '0' loop
            wait until rising_edge(clk);
          end loop;
          I := I + 1;
        else
          s_axi_wvalid <= '0';
          wait until rising_edge(clk);
        end if;
      end loop;
      s_axi_wlast <= '0';
      s_axi_wvalid <= '0';
      wait until rising_edge(clk);
    end procedure axi_bwrite;

    procedure axi_bwrite_16 (address : in integer;
                             length  : in integer;
                             value : in imem_arr) is

      variable addr_vec : std_logic_vector(core_dbg_addrw - 1 downto 0)
        := std_logic_vector(to_unsigned(address, core_dbg_addrw));

    begin
      s_axi_wstrb <= "1100";
      s_axi_awsize <= "001";
      s_axi_awlen <= std_logic_vector(to_unsigned(length-1, 8));

      s_axi_awvalid <= '1';
      s_axi_awaddr <= std_logic_vector(resize(unsigned(addr_vec), 32));
      while s_axi_awready = '0' loop
        wait until rising_edge(clk);
      end loop;
      s_axi_awvalid <= '0';

      for I in 0 to length-1 loop
        if I = length-1 then
          s_axi_wlast <= '1';
        end if;

        s_axi_wvalid <= '1';
        if s_axi_wstrb = "1100" then
          s_axi_wdata(15 downto 0) <= value(I);
          s_axi_wdata(31 downto 16) <= (others => '0');
        else
          s_axi_wdata(15 downto 0) <= (others => '0');
          s_axi_wdata(31 downto 16) <= value(I);
        end if;

        s_axi_wstrb <= not s_axi_wstrb;
        wait until rising_edge(clk);
        while s_axi_wready = '0' loop
          wait until rising_edge(clk);
        end loop;
      end loop;

      s_axi_wlast  <= '0';
      s_axi_wvalid <= '0';
      s_axi_wstrb  <= (others => '1');
      s_axi_awsize <= "010";
      wait until rising_edge(clk);
    end procedure axi_bwrite_16;

    procedure dbg_command (command : in integer) is
      variable cmdreg : std_logic_vector(core_dbg_dataw - 1 downto 0) := (others => '0');
    begin
      cmdreg(command) := '1';
      axi_write(TTA_DEBUG_CMD*4, cmdreg);
    end procedure dbg_command;

    procedure axi_read (constant address : in integer;
                        variable value : out std_logic_vector(core_dbg_dataw - 1 downto 0)) is

      variable addr_vec : std_logic_vector(core_dbg_addrw - 1 downto 0)
        := std_logic_vector(to_unsigned(address, core_dbg_addrw));

    begin
      s_axi_arlen <= (others => '0');
      s_axi_arvalid <= '1';

      s_axi_araddr <=std_logic_vector(resize(unsigned(addr_vec), 32));
      while s_axi_arready = '0' loop
        wait until rising_edge(clk);
      end loop;
      s_axi_arvalid <= '0';
      --s_axi_awvalid <= '0';
      s_axi_rready <= '1';

      while s_axi_rvalid = '0' loop
        wait until rising_edge(clk);
      end loop;
      value := s_axi_rdata;
      wait until rising_edge(clk);
      s_axi_rready <= '0';

    end procedure axi_read;

    procedure axi_bread (constant address : in integer;
                        constant length : in integer;
                        signal value : inout data_arr;
                        variable seed1 : inout integer;
                        variable seed2 : inout integer) is

      variable addr_vec : std_logic_vector(core_dbg_addrw - 1 downto 0)
        := std_logic_vector(to_unsigned(address, core_dbg_addrw));
      variable I : integer := 0;
      variable rand : real;
    begin
      wait until rising_edge(clk);
      s_axi_arlen <= std_logic_vector(to_unsigned(length-1, 8));

      s_axi_arvalid <= '1';
      s_axi_araddr <= std_logic_vector(resize(unsigned(addr_vec), 32));
      wait until rising_edge(clk);
      while s_axi_arready = '0' loop
        wait until rising_edge(clk);
      end loop;

      s_axi_arvalid <= '0';

      while I < length loop
        uniform(seed1, seed2, rand);
        if rand > stall_threshold then
          s_axi_rready <= '1';

          wait until rising_edge(clk);
          while s_axi_rvalid = '0' loop
            wait until rising_edge(clk);
          end loop;

          value(I) <= s_axi_rdata;
          I := I+1;
        else
          s_axi_rready <= '0';
          wait until rising_edge(clk);
        end if;
      end loop;
      wait until rising_edge(clk);

    end procedure axi_bread;


    type imem_slv_array is array (natural range <>) of
      std_logic_vector (IMEMDATAWIDTH-1 downto 0);
    variable imem_r : imem_slv_array (0 to 2**IMEMADDRWIDTH-1);
    variable imem_word : std_logic_vector(IMEMDATAWIDTH-1 downto 0);
    constant imem_word_padding : std_logic_vector(64-IMEMDATAWIDTH-1 downto 0)
                               := (others => '0');
    variable imem_word_padded : std_logic_vector(63 downto 0);
    variable imem_data : imem_arr(0 to 3);
    variable i : integer;
    variable good : boolean;
    file mem_init              : text;
    variable line_in           : line;
    variable value : std_logic_vector(31 downto 0);

    variable line_out : line;
    file logfile      : text;

    variable seed1    : integer := 733035985;
    variable seed2    : integer := 787335972;
    variable rand     : real;
    variable val_in   : std_logic_vector(31 downto 0);
  begin
      file_open(logfile, log_path, write_mode);

      nreset <= '0';

      s_axi_awid <= (others => '0');
      s_axi_awsize <= "010";
      s_axi_awburst <= "01";

      s_axi_arid <= (others => '0');
      s_axi_arlen <= (others => '0');
      s_axi_arsize <= "010";
      s_axi_arburst <= "01";

      s_axi_awaddr <= (others => '0');
      s_axi_araddr <= (others => '0');
      s_axi_wdata  <= (others => '0');
      s_axi_wlast <= '0';

      s_axi_wvalid <= '0';
      s_axi_arvalid <= '0';
      s_axi_awvalid <= '0';
      s_axi_bready <= '1';
      s_axi_wstrb <= (others=>'1');
      s_axi_rready <= '0';


      wait until rising_edge(clk);
      wait until rising_edge(clk);

      nreset <= '1';

      wait until rising_edge(clk);
      wait until rising_edge(clk);
      wait until rising_edge(clk);

      i := 0;
      file_open(mem_init, imem_image, read_mode);
      while (not endfile(mem_init) and i < imem_r'length) loop
        readline(mem_init, line_in);
        read(line_in, imem_word, good);
        assert good
          report "Read error in memory initialization file"
          severity failure;

        -- This supports instruction widths between 32 and 64, restrict with BEM
        imem_word_padded := imem_word_padding & imem_word;

        imem_data(0) := imem_word_padded(15 downto 0);
        imem_data(1) := imem_word_padded(31 downto 16);
        imem_data(2) := imem_word_padded(47 downto 32);
        imem_data(3) := imem_word_padded(63 downto 48);
        
        axi_bwrite_16(i*8   + IMEM_OFFSET, 4, imem_data);

        i        := i+1;
      end loop;

      -- IMEM initialization complete

    -- Zero-initialize parameter memory
    for i in integer range 0 to 2**10-1 loop
      axi_write(PMEM_OFFSET+i*4, (others=>'0'));
    end loop;

    axi_write(DMEM_OFFSET, X"ABAD1D3A");

    axi_read(TTA_CYCLECNT*4, value);
    axi_read(TTA_DEVICECLASS*4, value);
    axi_read(TTA_DEVICE_ID*4, value);
    axi_read(TTA_INTERFACE_TYPE*4, value);

    axi_read(TTA_DMEM_SIZE_LOW*4, value);
    write(line_out, "DMEM size: " & integer'image(to_integer(unsigned(value))));
    writeline(logfile, line_out);

    axi_read(TTA_CQMEM_SIZE_LOW*4, value);
    write(line_out, "CQMEM size: " & integer'image(to_integer(unsigned(value))));
    writeline(logfile, line_out);

    axi_read(TTA_IMEM_SIZE*4, value);
    write(line_out, "IMEM size: " & integer'image(to_integer(unsigned(value))));
    writeline(logfile, line_out);

    -- Lift softreset
    dbg_command(DEBUG_CMD_CONTINUE);

    value := (others=>'0');

    while value = X"00000000" loop
      -- Generate some dmem traffic to test whether access conflicts work...
      uniform(seed1, seed2, rand);
      val_in := std_logic_vector(
                        to_signed(integer((rand-0.5)*real(2147483647)), 32));
      for i in vals_in'range loop
        uniform(seed1, seed2, rand);
        vals_in(i) <= std_logic_vector(
                        to_signed(integer((rand-0.5)*real(2147483647)), 32));
      end loop;
      axi_write(DMEM_OFFSET+1024, val_in);
      axi_read(DMEM_OFFSET+1024, value);
      val_out <= value;
      wait until rising_edge(clk);
      assert val_in = value
        report "Read value doesn't match written value (single access). "
                 & "Expected: " & integer'image(to_integer(signed(val_in)))
                 & ", got: " & integer'image(to_integer(signed(val_out)))
        severity failure;

      axi_bwrite(DMEM_OFFSET + 1024, vals_in'high+1, vals_in, seed1, seed2);
      axi_bread(DMEM_OFFSET + 1024, vals_in'high+1, vals_out, seed1, seed2);

      for i in vals_in'range loop
        assert vals_in(i) = vals_out(i)
          report "Read value doesn't match written value (burst access). "
                 & "Expected: " & integer'image(to_integer(signed(vals_in(i))))
                 & ", got: " & integer'image(to_integer(signed(vals_out(i))))
          severity failure;
      end loop;

      -- Poll program completion
      axi_read(PMEM_OFFSET+4, value);
    end loop;

    -- Read DMEM by way of a 64-access burst
    axi_bread(DMEM_OFFSET, 64, in_data, seed1, seed2);
    for i in 0 to 63 loop
        write(line_out, "Data at address " & integer'image(i) & ": " & integer'image(to_integer(signed(in_data(i)))));
        writeline(logfile, line_out);
    end loop;
    clk_ena  <= '0';
    wait;
  end process;

end testbench;

