nck_sw_data = Proto("nck_sw_data", "NCKernel SW Data")
nck_sw_feedback = Proto("nck_sw_feedback", "NCKernel SW Feedback")

nck_sw_data.fields.packet_type = ProtoField.uint8("nck_sw_data.packet_type", "Packet Type", base.DEC)
nck_sw_data.fields.packet_no = ProtoField.uint16("nck_sw_data.packet_no", "Packet Number", base.DEC)
nck_sw_data.fields.flags = ProtoField.uint8("nck_sw_data.flags", "Flag field", base.DEC)
nck_sw_data.fields.order = ProtoField.uint8("nck_sw_data.order", "Elements in the coefficient vector", base.DEC)
nck_sw_data.fields.seqno = ProtoField.uint32("nck_sw_data.seqno", "Sequence number", base.DEC)
nck_sw_data.fields.data = ProtoField.uint8("nck_sw_data.data", "Missing data", base.DEC)
nck_sw_data.fields.systematic = ProtoField.uint8("nck_sw_data.systematic", "Systematic flag", base.DEC)
nck_sw_data.fields.coefficients = ProtoField.bytes("nck_sw_data.coefficients", "Coding coefficients", base.DOT)

nck_sw_feedback.fields.packet_type = ProtoField.uint8("nck_sw_feedback.packet_type", "Packet Type", base.DEC)
nck_sw_feedback.fields.packet_no = ProtoField.uint16("nck_sw_feedback.packet_no", "Last received packet number", base.DEC)
nck_sw_feedback.fields.feedback_no = ProtoField.uint16("nck_sw_feedback.feedback_no", "Feedback number", base.DEC)
nck_sw_feedback.fields.seqno = ProtoField.uint32("nck_sw_feedback.seqno", "Sequence number", base.DEC)
nck_sw_feedback.fields.order = ProtoField.uint8("nck_sw_feedback.order", "Bits in the missing symbols mask", base.DEC)
nck_sw_feedback.fields.first_missing = ProtoField.uint32("nck_sw_feedback.first_missing", "First missing sequence number", base.DEC)
nck_sw_feedback.fields.missing = ProtoField.bytes("nck_sw_feedback.missing", "Missing data", base.DOT)

function nck_sw_data.dissector(buffer, pinfo, tree)
	local subtree = tree:add(nck_sw_data, buffer)
	pinfo.cols.protocol = "NCK SW data"

	subtree:add(nck_sw_data.fields.packet_type, buffer(0, 1))
	subtree:add(nck_sw_data.fields.order, buffer(1, 1))
	subtree:add(nck_sw_data.fields.flags, buffer(2, 1))
	subtree:add(nck_sw_data.fields.packet_no, buffer(4, 2))
	subtree:add(nck_sw_data.fields.seqno, buffer(6, 4))
	subtree:add(nck_sw_data.fields.systematic, buffer(10, 1))

	local order = buffer(1, 1):uint()

	-- TODO: if it's systematic, we could even call another dissector ....
	if buffer(10,1):uint() == 0 then
		subtree:add(nck_sw_data.fields.coefficients, buffer(11, 2^order))
	end

--	subtree:add(nck_sw_data.fields.data, buffer(6, buffer:len() - 6))
end

function nck_sw_feedback.dissector(buffer, pinfo, tree)
	local subtree = tree:add(nck_sw_feedback, buffer)
	pinfo.cols.protocol = "NCK SW feedback"

	subtree:add(nck_sw_feedback.fields.packet_type, buffer(0, 1))
	subtree:add(nck_sw_feedback.fields.order, buffer(1, 1))
	subtree:add(nck_sw_feedback.fields.packet_no, buffer(2, 2))
	subtree:add(nck_sw_feedback.fields.feedback_no, buffer(4, 2))
	subtree:add(nck_sw_feedback.fields.seqno, buffer(8, 4))
	subtree:add(nck_sw_feedback.fields.first_missing, buffer(12, 4))

	local order = buffer(1, 1):uint()

	subtree:add(nck_sw_feedback.fields.missing, buffer(16, (2^order)/8))
end

udp_table = DissectorTable.get("udp.port")
udp_table:add(7867,nck_sw_data)
