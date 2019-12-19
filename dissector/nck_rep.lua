nck_rep_data = Proto("nck_rep_data", "NCKernel Repetition Data")
nck_rep_feedback = Proto("nck_rep_feedback", "NCKernel Repetition Feedback")

nck_rep_data.fields.packet_type = ProtoField.uint8("nck_rep_data.packet_type", "Packet Type", base.DEC)
nck_rep_data.fields.pktno = ProtoField.uint32("nck_rep_data.pktno", "Packet Number", base.DEC)
nck_rep_data.fields.seqno = ProtoField.uint32("nck_rep_data.seqno", "Sequence Number", base.DEC)

nck_rep_feedback.fields.packet_type = ProtoField.uint8("nck_rep_feedback.packet_type", "Packet Type", base.DEC)
nck_rep_feedback.fields.pktno = ProtoField.uint32("nck_rep_feedback.pktno", "Packet Number", base.DEC)
nck_rep_feedback.fields.seqno = ProtoField.uint32("nck_rep_feedback.seqno", "Sequence Number", base.DEC)
nck_rep_feedback.fields.mask_size = ProtoField.uint16("nck_rep_feedback.mask_size", "Bitmask Size", base.DEC)

function nck_rep_data.dissector(buffer, pinfo, tree)
	local subtree = tree:add(nck_rep_data, buffer)
	pinfo.cols.protocol = "NCK Rep data"

	subtree:add(nck_rep_data.fields.packet_type, buffer(0, 1))
	subtree:add(nck_rep_data.fields.pktno, buffer(4, 4))
	subtree:add(nck_rep_data.fields.seqno, buffer(8, 4))
end

function nck_rep_feedback.dissector(buffer, pinfo, tree)
	local subtree = tree:add(nck_rep_feedback, buffer)
	pinfo.cols.protocol = "NCK Rep feedback"

	subtree:add(nck_rep_feedback.fields.packet_type, buffer(0, 1))
	subtree:add(nck_rep_feedback.fields.pktno, buffer(4, 4))
	subtree:add(nck_rep_feedback.fields.seqno, buffer(8, 4))
	subtree:add(nck_rep_feedback.fields.mask_size, buffer(12, 2))
end
